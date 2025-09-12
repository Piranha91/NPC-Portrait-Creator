#define GLM_ENABLE_EXPERIMENTAL
#include "NifModel.h"
#include "Skeleton.h"
#include "Shader.h"
#include "TextureManager.h" 
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 
#include <limits>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/norm.hpp>  // gives length2() and distance2()
#include <chrono>

// Vertex structure used for processing mesh data, now includes skinning info
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec4 color;
    glm::ivec4 boneIDs = glm::ivec4(0);
    glm::vec4 weights = glm::vec4(0.0f);
};

// Helper function to calculate the centroid of a set of vertices
glm::vec3 CalculateCentroid(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        return glm::vec3(0.0f);
    }
    glm::vec3 sum(0.0f);
    for (const auto& v : vertices) {
        sum += v.pos;
    }
    return sum / static_cast<float>(vertices.size());
}

// Helper function to get the full world transform of any scene graph object (Node or Shape)
nifly::MatTransform GetAVObjectTransformToGlobal(const nifly::NifFile& nifFile, nifly::NiAVObject* obj, bool debugMode = false) {
    if (!obj) {
        return nifly::MatTransform();
    }
    if (debugMode) std::cout << "      [Debug] GetAVObjectTransformToGlobal for '" << obj->name.get() << "':\n";
    nifly::MatTransform xform = obj->GetTransformToParent();
    nifly::NiNode* parent = nifFile.GetParentNode(obj);
    while (parent) {
        if (debugMode) std::cout << "        - Traversing up to parent: '" << parent->name.get() << "'\n";
        xform = parent->GetTransformToParent().ComposeTransforms(xform);
        parent = nifFile.GetParentNode(parent);
    }
    if (debugMode) std::cout << "      [Debug] Final world transform calculated.\n";
    return xform;
}

// Helper to convert NIF blend modes to OpenGL blend modes
GLenum NifBlendToGL(unsigned int nifBlend) {
    switch (nifBlend) {
    case 0: return GL_ONE;          case 1: return GL_ZERO;
    case 2: return GL_SRC_COLOR;    case 3: return GL_ONE_MINUS_SRC_COLOR;
    case 4: return GL_DST_COLOR;    case 5: return GL_ONE_MINUS_DST_COLOR;
    case 6: return GL_SRC_ALPHA;    case 7: return GL_ONE_MINUS_SRC_ALPHA;
    case 8: return GL_DST_ALPHA;    case 9: return GL_ONE_MINUS_DST_ALPHA;
    case 10: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

// --- MeshShape Methods ---

void MeshShape::draw() const {
    if (VAO != 0 && indexCount > 0) {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, 0);
        glBindVertexArray(0);
    }
}

void MeshShape::cleanup() {
    if (VAO != 0) {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteBuffers(1, &EBO);
        VAO = VBO = EBO = 0;
        indexCount = 0;
    }
}

// --- NifModel Methods ---

NifModel::NifModel() {}

NifModel::~NifModel() {
    cleanup();
}

bool NifModel::load(const std::string& nifPath, TextureManager& textureManager, const Skeleton* skeleton) {
    bool debugMode = true;

    cleanup();
    if (nif.Load(nifPath) != 0) {
        std::cerr << "Error: Failed to load NIF file: " << nifPath << std::endl;
        return false;
    }

    const auto& shapeList = nif.GetShapes();
    if (shapeList.empty()) {
        std::cerr << "Warning: NIF file contains no shapes." << std::endl;
        return true;
    }

    // Reset bounds for the new model
    minBounds = glm::vec3(std::numeric_limits<float>::max());
    maxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    headMinBounds = glm::vec3(std::numeric_limits<float>::max());
    headMaxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    bHasEyeCenter = false;

    // Determine a shared 'accessory offset' transform from the head shape, if it exists.
    // This helps correctly place parts like eyes/brows that have no transform of their own.
    nifly::MatTransform accessoryOffset;
    for (auto* shape : shapeList) {
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
            for (const auto& partition : skinInst->partitions) {
                if (partition.partID == 30 || partition.partID == 230) { // Head partitions
                    accessoryOffset = GetAVObjectTransformToGlobal(nif, shape, false);
                    goto found_head;
                }
            }
        }
    }
found_head:

    for (auto* niShape : shapeList) {
        auto start_preprocess = std::chrono::high_resolution_clock::now();
        if (debugMode) std::cout << "\n--- Processing Shape: " << niShape->name.get() << " ---\n";
        if (!niShape || (niShape->flags & 1)) continue;

        const auto* vertices = nif.GetVertsForShape(niShape);
        if (!vertices || vertices->empty()) continue;

        std::vector<Vertex> vertexData(vertices->size());
        const auto* colors = nif.GetColorsForShape(niShape->name.get());

        for (size_t i = 0; i < vertices->size(); ++i) {
            vertexData[i].pos = glm::vec3((*vertices)[i].x, (*vertices)[i].y, (*vertices)[i].z);
            const auto* normals = nif.GetNormalsForShape(niShape);
            if (normals && i < normals->size()) vertexData[i].normal = glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
            else vertexData[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
            const auto* uvs = nif.GetUvsForShape(niShape);
            if (uvs && i < uvs->size()) vertexData[i].texCoords = glm::vec2((*uvs)[i].u, (*uvs)[i].v);
            else vertexData[i].texCoords = glm::vec2(0.0f, 0.0f);
            if (colors && i < colors->size()) {
                const auto& c = (*colors)[i];
                vertexData[i].color = glm::vec4(c.r, c.g, c.b, c.a);
            }
            else {
                vertexData[i].color = glm::vec4(1.0f);
            }
        }

        MeshShape mesh;
        std::string shapeName = niShape->name.get();

        if (const auto* triShape = dynamic_cast<const nifly::BSTriShape*>(niShape)) {
            if (triShape->HasEyeData()) {
                mesh.isEye = true;
            }
        }

        // --- BASE TRANSFORM CALCULATION ---
        // This calculates the object's base transform in the world, before skinning.
        nifly::MatTransform niflyTransform = GetAVObjectTransformToGlobal(nif, niShape, false);
        if (niflyTransform.IsNearlyEqualTo(nifly::MatTransform())) {
            bool isLocalSpacePart = (
                shapeName.find("Eyes") != std::string::npos ||
                shapeName.find("Mouth") != std::string::npos ||
                shapeName.find("Teeth") != std::string::npos ||
                shapeName.find("Brows") != std::string::npos
                );
            if (isLocalSpacePart) {
                if (debugMode) std::cout << "    [Debug] Applying accessory offset for local-space part '" << shapeName << "'.\n";
                niflyTransform = accessoryOffset;
            }
        }
        mesh.transform = glm::transpose(glm::make_mat4(&niflyTransform.ToMatrix()[0]));

        // --- GPU SKINNING DATA EXTRACTION ---
        if (niShape->IsSkinned()) {
            mesh.isSkinned = true;
            auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef());
            auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef);
            auto* skinPartition = nif.GetHeader().GetBlock<nifly::NiSkinPartition>(skinInst->skinPartitionRef);

            if (skinInst && skinData && skinPartition) {
                if (debugMode) std::cout << "    [Debug] Extracting skinning data for GPU...\n";

                // 1. Build the final bone matrix palette for this mesh
                mesh.boneMatrices.resize(skinData->bones.size());
                auto boneRefIt = skinInst->boneRefs.begin();
                for (size_t i = 0; i < skinData->bones.size() && boneRefIt != skinInst->boneRefs.end(); ++i, ++boneRefIt) {
                    auto* boneNode = nif.GetHeader().GetBlock<nifly::NiNode>(*boneRefIt);
                    if (!boneNode) continue;

                    std::string boneName = boneNode->name.get();
                    glm::mat4 boneWorld = glm::mat4(1.0f);
                    if (skeleton && skeleton->hasBone(boneName)) {
                        boneWorld = skeleton->getBoneTransform(boneName);
                    }
                    else {
                        auto boneWorld_n = GetAVObjectTransformToGlobal(nif, boneNode, false);
                        boneWorld = glm::transpose(glm::make_mat4(&boneWorld_n.ToMatrix()[0]));
                    }
                    const auto& skinToBone_n = skinData->bones[i].boneTransform;
                    glm::mat4 skinToBone = glm::transpose(glm::make_mat4(&skinToBone_n.ToMatrix()[0]));
                    mesh.boneMatrices[i] = boneWorld * skinToBone;
                }

                // 2. Assign bone indices and weights to each vertex
                for (const auto& partition : skinPartition->partitions) {
                    if (!partition.hasVertexMap || !partition.hasVertexWeights || !partition.hasBoneIndices) continue;
                    for (uint16_t i = 0; i < partition.numVertices; ++i) {
                        uint16_t vertIndex = partition.vertexMap[i];
                        if (vertIndex >= vertexData.size()) continue;

                        const float weights[] = { partition.vertexWeights[i].w1, partition.vertexWeights[i].w2, partition.vertexWeights[i].w3, partition.vertexWeights[i].w4 };
                        const uint8_t indices[] = { partition.boneIndices[i].i1, partition.boneIndices[i].i2, partition.boneIndices[i].i3, partition.boneIndices[i].i4 };

                        for (uint16_t k = 0; k < partition.numWeightsPerVertex; ++k) {
                            if (weights[k] > 0.0f) {
                                uint16_t globalBoneIndex = partition.bones[indices[k]];
                                vertexData[vertIndex].boneIDs[k] = globalBoneIndex;
                                vertexData[vertIndex].weights[k] = weights[k];
                            }
                        }
                    }
                }
            }
        }

        // --- COMMON LOGIC ---

        if (shapeName.find("Eyes") != std::string::npos) {
            glm::vec3 originalCentroid = CalculateCentroid(vertexData);
            eyeCenter = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
            bHasEyeCenter = true;
        }

        const nifly::NiShader* shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
        }

        // Bounds calculation (performed on un-skinned vertices; might not be perfectly accurate after posing)
        glm::mat4 boundsTransform = mesh.transform;
        glm::vec3 shapeMinBounds(std::numeric_limits<float>::max());
        glm::vec3 shapeMaxBounds(std::numeric_limits<float>::lowest());
        bool isAccessoryPart = false;
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(niShape->SkinInstanceRef())) {
            for (const auto& partition : skinInst->partitions) {
                if (partition.partID == 131) {
                    isAccessoryPart = true;
                    break;
                }
            }
        }
        if (!isAccessoryPart) {
            std::string lowerShapeName = shapeName;
            std::transform(lowerShapeName.begin(), lowerShapeName.end(), lowerShapeName.begin(), ::tolower);
            if (lowerShapeName.find("hair") != std::string::npos || lowerShapeName.find("scalp") != std::string::npos) {
                isAccessoryPart = true;
            }
        }

        for (const auto& vert : vertexData) {
            glm::vec3 tv3 = glm::vec3(boundsTransform * glm::vec4(vert.pos, 1.0f));
            minBounds = glm::min(minBounds, tv3);
            maxBounds = glm::max(maxBounds, tv3);
            shapeMinBounds = glm::min(shapeMinBounds, tv3);
            shapeMaxBounds = glm::max(shapeMaxBounds, tv3);
            if (!isAccessoryPart) {
                headMinBounds = glm::min(headMinBounds, tv3);
                headMaxBounds = glm::max(headMaxBounds, tv3);
            }
        }
        mesh.boundsCenter = (shapeMinBounds + shapeMaxBounds) * 0.5f;

        // Texture and material property loading
        if (shader && shader->HasTextureSet()) {
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                for (size_t i = 0; i < textureSet->textures.size(); ++i) {
                    std::string texPath = textureSet->textures[i].get();
                    if (texPath.empty()) continue;
                    switch (i) {
                    case 0: mesh.diffuseTextureID = textureManager.loadTexture(texPath); break;
                    case 1: mesh.normalTextureID = textureManager.loadTexture(texPath); break;
                    case 2: mesh.skinTextureID = textureManager.loadTexture(texPath); break;
                    case 3: mesh.detailTextureID = textureManager.loadTexture(texPath); break;
                    case 6: mesh.faceTintColorMaskID = textureManager.loadTexture(texPath); break;
                    case 7: mesh.specularTextureID = textureManager.loadTexture(texPath); break;
                    default: break;
                    }
                }
            }
        }
        if (const auto* bslsp = dynamic_cast<const nifly::BSLightingShaderProperty*>(shader)) {
            mesh.doubleSided = (bslsp->shaderFlags2 & (1U << 4));
            mesh.zBufferWrite = (bslsp->shaderFlags2 & (1U << 0));
            const auto shaderType = bslsp->GetShaderType();
            if (shaderType == nifly::BSLSP_HAIRTINT) {
                mesh.hasTintColor = true;
                const auto& color = bslsp->hairTintColor;
                mesh.tintColor = glm::vec3(color.x, color.y, color.z);
            }
            else if (shaderType == nifly::BSLSP_SKINTINT || shaderType == nifly::BSLSP_FACE) {
                mesh.hasTintColor = true;
                const auto& color = bslsp->skinTintColor;
                mesh.tintColor = glm::vec3(color.x, color.y, color.z);
            }
        }
        if (auto* alphaProp = nif.GetAlphaProperty(niShape)) {
            mesh.hasAlphaProperty = true;
            uint16_t flags = alphaProp->flags;
            mesh.alphaBlend = (flags & 1);
            mesh.alphaTest = (flags & (1 << 9));
            mesh.alphaThreshold = static_cast<float>(alphaProp->threshold) / 255.0f;
            mesh.srcBlend = NifBlendToGL((flags >> 1) & 0x0F);
            mesh.dstBlend = NifBlendToGL((flags >> 5) & 0x0F);
        }

        // VAO/VBO/EBO setup
        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);
        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(Vertex), vertexData.data(), GL_STATIC_DRAW);

        std::vector<nifly::Triangle> triangles;
        niShape->GetTriangles(triangles);
        if (!triangles.empty()) {
            std::vector<unsigned short> final_indices;
            final_indices.reserve(triangles.size() * 3);
            for (const auto& tri : triangles) {
                final_indices.push_back(tri.p1);
                final_indices.push_back(tri.p2);
                final_indices.push_back(tri.p3);
            }
            mesh.indexCount = static_cast<GLsizei>(final_indices.size());
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, final_indices.size() * sizeof(unsigned short), final_indices.data(), GL_STATIC_DRAW);
        }

        // Standard attributes
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

        // New skinning attributes
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, boneIDs));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, weights));

        glBindVertexArray(0);

        // Sort into render passes
        if (mesh.hasAlphaProperty) {
            if (mesh.alphaBlend) transparentShapes.push_back(mesh);
            else if (mesh.alphaTest) alphaTestShapes.push_back(mesh);
            else opaqueShapes.push_back(mesh);
        }
        else {
            opaqueShapes.push_back(mesh);
        }

        auto end_preprocess = std::chrono::high_resolution_clock::now();
        auto duration_preprocess = std::chrono::duration_cast<std::chrono::milliseconds>(end_preprocess - start_preprocess);
        std::cout << "    [Profile] Data extraction took: " << duration_preprocess.count() << " ms\n";
    }

    if (debugMode) {
        std::cout << "\n--- Load Complete ---\n";
        std::cout << "Model Center: (" << getCenter().x << ", " << getCenter().y << ", " << getCenter().z << ")\n";
        std::cout << "Model Bounds Size: (" << getBoundsSize().x << ", " << getBoundsSize().y << ", " << getBoundsSize().z << ")\n";
        std::cout << "---------------------\n\n";
    }

    return true;
}

void NifModel::draw(Shader& shader, const glm::vec3& cameraPos) {
    shader.use();
    shader.setInt("texture_diffuse1", 0);
    shader.setInt("texture_normal", 1);
    shader.setInt("texture_skin", 2);
    shader.setInt("texture_detail", 3);
    shader.setInt("texture_specular", 4);
    shader.setInt("texture_face_tint", 5);

    shader.setFloat("eye_fresnel_strength", 0.3f);
    shader.setFloat("eye_spec_power", 80.0f);

    // Get location for bone matrix uniform array (cache it for performance)
    static GLint boneMatricesLocation = glGetUniformLocation(shader.ID, "uBoneMatrices");

    // Helper lambda to render a single shape, setting all its unique uniforms
    auto render_shape = [&](const MeshShape& shape) {
        shader.setMat4("model", shape.transform);
        shader.setBool("is_eye", shape.isEye);
        shader.setBool("is_model_space", shape.isModelSpace); // Set model space uniform
        shader.setBool("has_tint_color", shape.hasTintColor);
        if (shape.hasTintColor) {
            shader.setVec3("tint_color", shape.tintColor);
        }

        // Set skinning uniforms for the vertex shader
        shader.setBool("uIsSkinned", shape.isSkinned);
        if (shape.isSkinned && !shape.boneMatrices.empty()) {
            glUniformMatrix4fv(boneMatricesLocation, shape.boneMatrices.size(), GL_FALSE, glm::value_ptr(shape.boneMatrices[0]));
        }

        // Bind all textures and set corresponding 'has_' flags for the fragment shader
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);

        shader.setBool("has_normal_map", shape.normalTextureID != 0);
        if (shape.normalTextureID != 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shape.normalTextureID); }

        shader.setBool("has_skin_map", shape.skinTextureID != 0);
        if (shape.skinTextureID != 0) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shape.skinTextureID); }

        shader.setBool("has_detail_map", shape.detailTextureID != 0);
        if (shape.detailTextureID != 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, shape.detailTextureID); }

        shader.setBool("has_specular_map", shape.specularTextureID != 0);
        if (shape.specularTextureID != 0) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shape.specularTextureID); }

        shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
        if (shape.faceTintColorMaskID != 0) { glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID); }

        shape.draw();
        };

    // --- PASS 1: OPAQUE OBJECTS ---
    // Render all fully opaque objects first. They will write to the depth buffer,
    // establishing the scene's depth for later passes.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    shader.setBool("use_alpha_test", false);
    for (const auto& shape : opaqueShapes) {
        render_shape(shape);
    }

    // --- PASS 2: ALPHA-TEST (CUTOUT) OBJECTS ---
    // Render objects with cutout transparency (like hair or grates).
    // These objects test against the depth buffer and also write to it.
    // MSAA is used to smooth the hard edges of the cutouts.
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    shader.setBool("use_alpha_test", true);
    for (const auto& shape : alphaTestShapes) {
        shader.setFloat("alpha_threshold", shape.alphaThreshold);

        if (shape.doubleSided) {
            glCullFace(GL_FRONT); // Pass 1: Draw back faces
            render_shape(shape);
            glCullFace(GL_BACK);  // Pass 2: Draw front faces
            render_shape(shape);
        }
        else {
            glCullFace(GL_BACK);
            render_shape(shape);
        }
    }
    shader.setBool("use_alpha_test", false);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    // --- PASS 3: TRANSPARENT (ALPHA-BLEND) OBJECTS ---
    // Render truly transparent objects last, sorted from back to front.
    // They test against the depth buffer but do not write to it to prevent
    // objects behind them from being culled incorrectly.
    if (!transparentShapes.empty()) {
        std::sort(transparentShapes.begin(), transparentShapes.end(),
            [&cameraPos](const MeshShape& a, const MeshShape& b) {
                return glm::distance2(a.boundsCenter, cameraPos) > glm::distance2(b.boundsCenter, cameraPos);
            });

        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE); // CRITICAL: Disable depth writes

        for (const auto& shape : transparentShapes) {
            glBlendFunc(shape.srcBlend, shape.dstBlend);
            if (shape.doubleSided) {
                glDisable(GL_CULL_FACE);
            }
            else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            render_shape(shape);
        }
    }

    // --- Reset to default OpenGL state ---
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void NifModel::cleanup() {
    for (auto& shape : opaqueShapes) shape.cleanup();
    opaqueShapes.clear();
    for (auto& shape : alphaTestShapes) shape.cleanup();
    alphaTestShapes.clear();
    for (auto& shape : transparentShapes) shape.cleanup();
    transparentShapes.clear();
    texturePaths.clear();
}

std::vector<std::string> NifModel::getTextures() const {
    return texturePaths;
}


