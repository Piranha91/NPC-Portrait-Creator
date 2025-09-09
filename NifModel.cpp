#define GLM_ENABLE_EXPERIMENTAL
#include "NifModel.h"
#include "Shader.h"
#include "TextureManager.h" 
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 
#include <limits>
#include <glm/gtx/string_cast.hpp>

// Vertex structure used for processing mesh data
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoords;
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

// --- HELPER FUNCTIONS FROM VERSION 2 ---

// Returns true if 'ancestor' is an ancestor of 'node' in the NIF scene graph.
static bool IsAncestor(const nifly::NifFile& nif, const nifly::NiNode* ancestor, nifly::NiNode* node) {
    if (!ancestor || !node) return false;
    for (nifly::NiNode* p = node; p != nullptr; p = nif.GetParentNode(p)) {
        if (p == ancestor) return true;
    }
    return false;
}

// Finds the lowest common ancestor of all bones in a NiSkinInstance.
static nifly::NiNode* FindSkeletonRootLCA(const nifly::NifFile& nif, nifly::NiSkinInstance* si) {
    if (!si) return nullptr;
    std::vector<nifly::NiNode*> bones;
    for (auto it = si->boneRefs.begin(); it != si->boneRefs.end(); ++it) {
        if (auto* b = nif.GetHeader().GetBlock<nifly::NiNode>(*it)) {
            bones.push_back(b);
        }
    }
    if (bones.empty()) return nullptr;
    for (nifly::NiNode* candidate = bones[0]; candidate != nullptr; candidate = nif.GetParentNode(candidate)) {
        bool allChildrenFound = true;
        for (size_t i = 1; i < bones.size(); ++i) {
            if (!IsAncestor(nif, candidate, bones[i])) {
                allChildrenFound = false;
                break;
            }
        }
        if (allChildrenFound) return candidate;
    }
    return nif.GetRootNode(); // Fallback
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

bool NifModel::load(const std::string& nifPath, TextureManager& textureManager) {
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

    // ===================================================================================
    // === HEURISTIC: DETERMINE NIF TYPE (Standard vs. Pre-translated)
    // ===================================================================================
    bool useCpuBake = false;
    nifly::MatTransform accessoryOffset;
    nifly::NiShape* headShape = nullptr;

    // 1. Find the main head shape
    for (auto* shape : shapeList) {
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
            for (const auto& partition : skinInst->partitions) {
                if (partition.partID == 30 || partition.partID == 230) { // Standard head part IDs
                    headShape = shape;
                    break;
                }
            }
        }
        if (headShape) break;
    }

    // 2. If a head was found, analyze it to determine the loading strategy.
    if (headShape) {
        if (debugMode) std::cout << "[NIF Analysis] Found main head part: '" << headShape->name.get() << "'\n";

        nifly::MatTransform headNodeTransform = GetAVObjectTransformToGlobal(nif, headShape, false);

        // Calculate the head's local vertex centroid
        glm::vec3 headLocalCentroid(0.0f);
        const auto* vertices = nif.GetVertsForShape(headShape);
        if (vertices && !vertices->empty()) {
            glm::vec3 sum(0.0f);
            for (const auto& v : *vertices) {
                sum += glm::vec3(v.x, v.y, v.z);
            }
            headLocalCentroid = sum / static_cast<float>(vertices->size());
        }

        if (debugMode) {
            std::cout << "[NIF Analysis] Head Node Transform is " << (headNodeTransform.IsNearlyEqualTo(nifly::MatTransform()) ? "Identity" : "Not Identity") << ".\n";
            std::cout << "[NIF Analysis] Head Local Centroid length is " << glm::length(headLocalCentroid) << ".\n";
        }

        // Heuristic: If the node transform is identity BUT the vertices are far from the origin,
        // it's a pre-translated model that needs CPU baking.
        const float PRETRANSLATED_THRESHOLD = 10.0f;
        if (headNodeTransform.IsNearlyEqualTo(nifly::MatTransform()) && glm::length(headLocalCentroid) > PRETRANSLATED_THRESHOLD) {
            useCpuBake = true;
            std::cout << "[NIF Analysis] DECISION: Pre-translated model detected. Using CPU Bake strategy.\n";
        }
        else {
            useCpuBake = false;
            std::cout << "[NIF Analysis] DECISION: Standard model detected. Using GPU Transform strategy.\n";
            // For standard models, calculate the accessory offset now (V1 logic)
            accessoryOffset = headNodeTransform;
        }
    }
    else {
        // If no head is found, fallback to the more robust CPU bake method as a safety measure.
        useCpuBake = true;
        std::cout << "[NIF Analysis] No head part found. Defaulting to CPU Bake strategy.\n";
    }

    // ===================================================================================
    // === PROCESS ALL SHAPES USING THE CHOSEN STRATEGY
    // ===================================================================================
    for (auto* niShape : shapeList) {
        if (debugMode) std::cout << "\n--- Processing Shape: " << niShape->name.get() << " ---\n";
        if (!niShape || (niShape->flags & 1)) continue;

        const auto* vertices = nif.GetVertsForShape(niShape);
        if (!vertices || vertices->empty()) continue;

        std::vector<Vertex> vertexData(vertices->size());
        for (size_t i = 0; i < vertices->size(); ++i) {
            vertexData[i].pos = glm::vec3((*vertices)[i].x, (*vertices)[i].y, (*vertices)[i].z);
            const auto* normals = nif.GetNormalsForShape(niShape);
            if (normals && i < normals->size()) vertexData[i].normal = glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
            else vertexData[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
            const auto* uvs = nif.GetUvsForShape(niShape);
            if (uvs && i < uvs->size()) vertexData[i].texCoords = glm::vec2((*uvs)[i].u, (*uvs)[i].v);
            else vertexData[i].texCoords = glm::vec2(0.0f, 0.0f);
        }

        MeshShape mesh;
        std::string shapeName = niShape->name.get();

        if (useCpuBake) {
            // --- STRATEGY 2: CPU Vertex Bake (For Pre-translated NIFs like NPC #2) ---
            auto* skinInst = niShape->IsSkinned() ? nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef()) : nullptr;
            if (skinInst) {
                auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef);
                auto* skinPartition = nif.GetHeader().GetBlock<nifly::NiSkinPartition>(skinInst->skinPartitionRef);

                if (skinData && skinPartition) {
                    std::vector<glm::mat4> boneWorldMatrices(skinData->bones.size());
                    auto boneRefIt = skinInst->boneRefs.begin();
                    for (size_t i = 0; i < skinData->bones.size() && boneRefIt != skinInst->boneRefs.end(); ++i, ++boneRefIt) {
                        auto* boneNode = nif.GetHeader().GetBlock<nifly::NiNode>(*boneRefIt);
                        if (!boneNode) continue;
                        auto boneWorld_n = GetAVObjectTransformToGlobal(nif, boneNode, false);
                        const auto& skinToBone_n = skinData->bones[i].boneTransform;
                        glm::mat4 boneWorld = glm::transpose(glm::make_mat4(&boneWorld_n.ToMatrix()[0]));
                        glm::mat4 skinToBone = glm::transpose(glm::make_mat4(&skinToBone_n.ToMatrix()[0]));
                        boneWorldMatrices[i] = boneWorld * skinToBone;
                    }

                    auto originalVertexData = vertexData;
                    std::fill(vertexData.begin(), vertexData.end(), Vertex{});
                    std::vector<float> totalWeights(originalVertexData.size(), 0.0f);
                    for (const auto& partition : skinPartition->partitions) {
                        if (!partition.hasVertexMap || !partition.hasVertexWeights || !partition.hasBoneIndices) continue;
                        for (uint16_t i = 0; i < partition.numVertices; ++i) {
                            uint16_t originalVertIndex = partition.vertexMap[i];
                            if (originalVertIndex >= originalVertexData.size()) continue;

                            const float weights[] = { partition.vertexWeights[i].w1, partition.vertexWeights[i].w2, partition.vertexWeights[i].w3, partition.vertexWeights[i].w4 };
                            const uint8_t indices[] = { partition.boneIndices[i].i1, partition.boneIndices[i].i2, partition.boneIndices[i].i3, partition.boneIndices[i].i4 };
                            for (uint16_t k = 0; k < partition.numWeightsPerVertex; ++k) {
                                float weight = weights[k];
                                if (weight <= 0.0f) continue;
                                uint16_t globalBoneIndex = partition.bones[indices[k]];
                                if (globalBoneIndex >= boneWorldMatrices.size()) continue;

                                const glm::mat4& boneMatrix = boneWorldMatrices[globalBoneIndex];
                                vertexData[originalVertIndex].pos += glm::vec3(boneMatrix * glm::vec4(originalVertexData[originalVertIndex].pos, 1.0f)) * weight;
                                glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(boneMatrix)));
                                vertexData[originalVertIndex].normal += (normalMatrix * originalVertexData[originalVertIndex].normal) * weight;
                                totalWeights[originalVertIndex] += weight;
                            }
                            vertexData[originalVertIndex].texCoords = originalVertexData[originalVertIndex].texCoords;
                        }
                    }

                    for (size_t i = 0; i < vertexData.size(); ++i) {
                        if (totalWeights[i] > 0.0f) {
                            vertexData[i].pos /= totalWeights[i];
                            vertexData[i].normal = glm::normalize(vertexData[i].normal);
                        }
                        else {
                            vertexData[i] = originalVertexData[i];
                        }
                    }
                    mesh.transform = glm::mat4(1.0f); // Vertices are now in world space
                }
                else {
                    mesh.transform = glm::transpose(glm::make_mat4(&GetAVObjectTransformToGlobal(nif, niShape).ToMatrix()[0]));
                }
            }
            else { // Unskinned shape in a CPU bake model
                mesh.transform = glm::transpose(glm::make_mat4(&GetAVObjectTransformToGlobal(nif, niShape).ToMatrix()[0]));
            }

        }
        else {
            // --- STRATEGY 1: GPU Transform (For Standard NIFs like NPC #1) ---
            nifly::MatTransform niflyTransform;
            nifly::MatTransform ownTransform = GetAVObjectTransformToGlobal(nif, niShape, false);

            // Default to using the shape's own transform from the scene graph.
            niflyTransform = ownTransform;

            // Heuristic: If a face part has an identity transform, it's likely "broken"
            // and needs to be manually placed using the head's offset. This is common
            // for eyes, mouth, and brows as your analysis pointed out.
            if (ownTransform.IsNearlyEqualTo(nifly::MatTransform())) {
                bool isLocalSpacePart = (
                    shapeName.find("Eyes") != std::string::npos ||
                    shapeName.find("Mouth") != std::string::npos ||
                    shapeName.find("Teeth") != std::string::npos || // Added for clarity
                    shapeName.find("Brows") != std::string::npos
                    );

                // This handles the "5 feet above the head" case for hair/scalps that
                // were exported at the origin without a transform.
                if (!isLocalSpacePart) {
                    glm::vec3 localCentroid = CalculateCentroid(vertexData);
                    const float LOCAL_SPACE_THRESHOLD = 1.0f; // A small threshold
                    if (glm::length(localCentroid) < LOCAL_SPACE_THRESHOLD) {
                        if (debugMode) std::cout << "    [Debug] Shape '" << shapeName << "' is at origin with no transform. Applying accessory offset as a fix.\n";
                        isLocalSpacePart = true;
                    }
                }

                if (isLocalSpacePart) {
                    if (debugMode) std::cout << "    [Debug] Applying accessory offset for local-space part '" << shapeName << "'.\n";
                    niflyTransform = accessoryOffset;
                }
                else {
                    if (debugMode) std::cout << "    [Debug] Using identity transform for pre-translated part '" << shapeName << "'.\n";
                    // It has an identity transform and is not a known local part,
                    // so its vertices are assumed to be pre-translated in world space.
                    // niflyTransform is already identity, so no change needed.
                }
            }
            else {
                if (debugMode) std::cout << "    [Debug] Using shape's own scene graph transform for '" << shapeName << "'.\n";
            }

            mesh.transform = glm::transpose(glm::make_mat4(&niflyTransform.ToMatrix()[0]));

            // --- RESTORED LOGGING BLOCK ---
            glm::vec3 originalCentroid = CalculateCentroid(vertexData);
            auto* skinInst = niShape->IsSkinned() ? nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef()) : nullptr;
            if (debugMode) {
                std::cout << "    [Debug] Calculated world transform. Applying to mesh.\n";
                glm::vec3 transformedCentroid = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
                std::cout << "    [Debug] --- Mesh Transformation (" << (skinInst ? "Skinned as Rigid" : "Unskinned") << ") ---\n";
                std::cout << "    [Debug] Original Centroid: " << glm::to_string(originalCentroid) << "\n";
                std::cout << "    [Debug] Transformation Matrix:\n" << glm::to_string(mesh.transform) << "\n";
                std::cout << "    [Debug] Transformed Centroid: " << glm::to_string(transformedCentroid) << "\n";
                std::cout << "    [Debug] ------------------------------------------\n";
            }
        }

        // --- COMMON LOGIC FOR BOTH STRATEGIES ---

        if (shapeName.find("Eyes") != std::string::npos) {
            glm::vec3 originalCentroid = CalculateCentroid(vertexData);
            eyeCenter = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
            bHasEyeCenter = true;
        }

        const nifly::NiShader* shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
        }

        // Bounds calculation
        glm::mat4 boundsTransform = mesh.transform;
        bool isHairPart = (shapeName.find("Hair") != std::string::npos);
        for (const auto& vert : vertexData) {
            glm::vec3 tv3 = glm::vec3(boundsTransform * glm::vec4(vert.pos, 1.0f));
            minBounds = glm::min(minBounds, tv3);
            maxBounds = glm::max(maxBounds, tv3);
            if (!isHairPart) {
                headMinBounds = glm::min(headMinBounds, tv3);
                headMaxBounds = glm::max(headMaxBounds, tv3);
            }
        }

        // Texture and material property loading (same for both paths)
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

        // VAO/VBO/EBO setup (same for both paths)
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
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));
        glBindVertexArray(0);

        if (mesh.alphaBlend || mesh.alphaTest) {
            transparentShapes.push_back(mesh);
        }
        else {
            opaqueShapes.push_back(mesh);
        }
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

    // --- PASS 1: OPAQUE OBJECTS ---
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    shader.setBool("use_alpha_test", false);

    for (const auto& shape : opaqueShapes) {
        shader.setMat4("model", shape.transform);

        shader.setBool("has_tint_color", shape.hasTintColor);
        if (shape.hasTintColor) {
            shader.setVec3("tint_color", shape.tintColor);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);
        shader.setBool("has_normal_map", shape.normalTextureID != 0);
        if (shape.normalTextureID != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shape.normalTextureID);
        }
        shader.setBool("has_skin_map", shape.skinTextureID != 0);
        if (shape.skinTextureID != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, shape.skinTextureID);
        }
        shader.setBool("has_detail_map", shape.detailTextureID != 0);
        if (shape.detailTextureID != 0) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, shape.detailTextureID);
        }
        shader.setBool("has_specular_map", shape.specularTextureID != .0f);
        if (shape.specularTextureID != 0) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, shape.specularTextureID);
        }
        shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
        if (shape.faceTintColorMaskID != 0) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID);
        }

        shape.draw();
    }

    // --- SORT TRANSPARENT OBJECTS ---
    std::sort(transparentShapes.begin(), transparentShapes.end(),
        [&cameraPos](const MeshShape& a, const MeshShape& b) {
            glm::vec3 posA = glm::vec3(a.transform[3]);
            glm::vec3 posB = glm::vec3(b.transform[3]);
            glm::vec3 diffA = posA - cameraPos;
            glm::vec3 diffB = posB - cameraPos;
            return glm::dot(diffA, diffA) > glm::dot(diffB, diffB);
        });

    // --- PASS 2: TRANSPARENT OBJECTS ---
    for (const auto& shape : transparentShapes) {
        shader.setMat4("model", shape.transform);

        if (shape.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);

        if (shape.alphaBlend) {
            glEnable(GL_BLEND);
            glBlendFunc(shape.srcBlend, shape.dstBlend);
        }
        else {
            glDisable(GL_BLEND);
        }

        if (shape.alphaTest) {
            shader.setBool("use_alpha_test", true);
            shader.setFloat("alpha_threshold", shape.alphaThreshold);
        }
        else {
            shader.setBool("use_alpha_test", false);
        }

        if (shape.alphaTest) {
            glDepthMask(GL_TRUE);
        }
        else {
            glDepthMask(shape.zBufferWrite ? GL_TRUE : GL_FALSE);
        }

        shader.setBool("has_tint_color", shape.hasTintColor);
        if (shape.hasTintColor) {
            shader.setVec3("tint_color", shape.tintColor);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);
        shader.setBool("has_normal_map", shape.normalTextureID != 0);
        if (shape.normalTextureID != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shape.normalTextureID);
        }
        shader.setBool("has_skin_map", shape.skinTextureID != 0);
        if (shape.skinTextureID != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, shape.skinTextureID);
        }
        shader.setBool("has_detail_map", shape.detailTextureID != 0);
        if (shape.detailTextureID != 0) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, shape.detailTextureID);
        }
        shader.setBool("has_specular_map", shape.specularTextureID != 0);
        if (shape.specularTextureID != 0) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, shape.specularTextureID);
        }
        shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
        if (shape.faceTintColorMaskID != 0) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID);
        }

        shape.draw();
    }

    // Reset to default OpenGL state after drawing all shapes
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void NifModel::cleanup() {
    for (auto& shape : opaqueShapes) {
        shape.cleanup();
    }
    opaqueShapes.clear();

    for (auto& shape : transparentShapes) {
        shape.cleanup();
    }
    transparentShapes.clear();

    texturePaths.clear();
}

std::vector<std::string> NifModel::getTextures() const {
    return texturePaths;
}
