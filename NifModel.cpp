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
#include <glm/gtx/string_cast.hpp> // For printing glm::mat4

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


// Helper function to convert NIF blend modes to OpenGL blend modes
GLenum NifBlendToGL(unsigned int nifBlend) {
    switch (nifBlend) {
    case 0: return GL_ONE;
    case 1: return GL_ZERO;
    case 2: return GL_SRC_COLOR;
    case 3: return GL_ONE_MINUS_SRC_COLOR;
    case 4: return GL_DST_COLOR;
    case 5: return GL_ONE_MINUS_DST_COLOR;
    case 6: return GL_SRC_ALPHA;
    case 7: return GL_ONE_MINUS_SRC_ALPHA;
    case 8: return GL_DST_ALPHA;
    case 9: return GL_ONE_MINUS_DST_ALPHA;
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

bool NifModel::load(const std::string& nifPath, TextureManager& textureManager) {
    // DEBUGGING FLAG
    bool debugMode = true;

    cleanup();
    if (nif.Load(nifPath) != 0) {
        std::cerr << "Error: Failed to load NIF file: " << nifPath << std::endl;
        return false;
    }

    const auto& shapeList = nif.GetShapes();

    // Reset bounds and eye tracking for the new model
    minBounds = glm::vec3(std::numeric_limits<float>::max());
    maxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    // --- NEW: Reset head-only bounds ---
    headMinBounds = glm::vec3(std::numeric_limits<float>::max());
    headMaxBounds = glm::vec3(std::numeric_limits<float>::lowest());

    bHasEyeCenter = false;
    if (shapeList.empty()) {
        std::cerr << "Warning: NIF file contains no shapes." << std::endl;
        return true;
    }

    // PASS 1: Find the main head transform.
    nifly::MatTransform headShapeTransform;
    bool isFaceGen = false;
    nifly::NiNode* headParentNode = nullptr;

    if (debugMode) std::cout << "\n[Debug] --- Finding Head Shape ---\n";
    for (auto* shape : shapeList) {
        if (debugMode) {
            std::cout << "  [Debug] Checking Shape: " << shape->GetBlockName() << ": " << shape->name.get() << "\n";
        }

        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
            if (debugMode && !skinInst->partitions.empty()) {
                std::cout << "    [Debug] Contains Partitions: ";
                for (size_t i = 0; i < skinInst->partitions.size(); ++i) {
                    std::cout << skinInst->partitions[i].partID << (i == skinInst->partitions.size() - 1 ? "" : ", ");
                }
                std::cout << "\n";
            }

            for (const auto& partition : skinInst->partitions) {
                // SBP_230_HEAD corresponds to a body part ID of 230
                if (partition.partID == 230) {
                    if (debugMode) std::cout << "    [Debug] ^^^ Found head shape via DismemberSkinInstance partition.\n";

                    headShapeTransform = GetAVObjectTransformToGlobal(nif, shape, false);
                    headParentNode = nif.GetParentNode(shape);
                    isFaceGen = true;
                    // No break here, continue scanning all shapes for logging purposes.
                }
            }
        }
    }
    if (debugMode) std::cout << "[Debug] --- End Head Shape Scan ---\n";

    // PASS 2: Process all shapes, applying the cached head transform to face parts.
    for (auto* niShape : shapeList) {
        if (debugMode) std::cout << "\n--- Processing Shape: " << niShape->name.get() << " ---\n";

        if (!niShape || (niShape->flags & 1)) {
            if (debugMode) std::cout << "  [Debug] Shape is hidden, skipping.\n";
            continue;
        }

        const auto* vertices = nif.GetVertsForShape(niShape);
        const auto* normals = nif.GetNormalsForShape(niShape);
        const auto* uvs = nif.GetUvsForShape(niShape);
        std::vector<nifly::Triangle> triangles;
        niShape->GetTriangles(triangles);
        if (!vertices || vertices->empty()) {
            if (debugMode) std::cout << "  [Debug] Shape has no vertices, skipping.\n";
            continue;
        }

        std::vector<Vertex> vertexData(vertices->size());
        for (size_t i = 0; i < vertices->size(); ++i) {
            vertexData[i].pos = glm::vec3((*vertices)[i].x, (*vertices)[i].y, (*vertices)[i].z);
            if (normals && i < normals->size()) {
                vertexData[i].normal = glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
            }
            else {
                vertexData[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (uvs && i < uvs->size()) {
                vertexData[i].texCoords = glm::vec2((*uvs)[i].u, (*uvs)[i].v);
            }
            else {
                vertexData[i].texCoords = glm::vec2(0.0f, 0.0f);
            }
        }

        glm::vec3 originalCentroid = CalculateCentroid(vertexData);
        MeshShape mesh;
        auto* skinInst = niShape->IsSkinned() ? nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef()) : nullptr;

        if (skinInst) {
            if (debugMode) std::cout << "  [Debug] Shape is SKINNED. Treating as a rigid object and ignoring skinning data.\n";
        }
        else {
            if (debugMode) std::cout << "  [Debug] Shape is UNskinned.\n";
        }

        nifly::MatTransform niflyTransform;
        std::string shapeName = niShape->name.get();

        if (isFaceGen) {
            if (shapeName.find("Eyes") != std::string::npos || shapeName.find("Mouth") != std::string::npos || shapeName.find("Brows") != std::string::npos) {
                if (debugMode) std::cout << "    [Debug] Inheriting transform from main head part.\n";
                niflyTransform = headShapeTransform;
            }
            else {
                niflyTransform = GetAVObjectTransformToGlobal(nif, niShape, debugMode);
            }
        }
        else {
            niflyTransform = GetAVObjectTransformToGlobal(nif, niShape, debugMode);
        }

        mesh.transform = glm::transpose(glm::make_mat4(&niflyTransform.ToMatrix()[0]));

        // --- NEW: Check for eyes and store center ---
        if (shapeName.find("Eyes") != std::string::npos) {
            eyeCenter = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
            bHasEyeCenter = true;
        }

        if (debugMode) {
            std::cout << "    [Debug] Calculated world transform. Applying to mesh.\n";
            glm::vec3 transformedCentroid = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
            std::cout << "    [Debug] --- Mesh Transformation (" << (skinInst ? "Skinned as Rigid" : "Unskinned") << ") ---\n";
            std::cout << "    [Debug] Original Centroid: " << glm::to_string(originalCentroid) << "\n";
            std::cout << "    [Debug] Transformation Matrix:\n" << glm::to_string(mesh.transform) << "\n";
            std::cout << "    [Debug] Transformed Centroid: " << glm::to_string(transformedCentroid) << "\n";
            std::cout << "    [Debug] ------------------------------------------\n";
        }

        const nifly::NiShader* shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
        }

        // --- MODIFIED: Bounding box calculation ---
        glm::mat4 boundsTransform = mesh.transform;
        bool isHairPart = (shapeName.find("Hair") != std::string::npos);

        for (const auto& vert : vertexData) {
            glm::vec4 transformedVert = boundsTransform * glm::vec4(vert.pos, 1.0f);
            glm::vec3 tv3 = glm::vec3(transformedVert);

            // Update overall bounds for all shapes
            minBounds = glm::min(minBounds, tv3);
            maxBounds = glm::max(maxBounds, tv3);

            // Update head-only bounds if it's not a hair part
            if (!isHairPart) {
                headMinBounds = glm::min(headMinBounds, tv3);
                headMaxBounds = glm::max(headMaxBounds, tv3);
            }
        }
        // --- END: MODIFIED BOUNDING BOX CALCULATION ---

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

        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);

        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(Vertex), vertexData.data(), GL_STATIC_DRAW);

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
            if (debugMode) std::cout << "  [Debug] Classifying shape as TRANSPARENT.\n";
            transparentShapes.push_back(mesh);
        }
        else {
            if (debugMode) std::cout << "  [Debug] Classifying shape as OPAQUE.\n";
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
        if (shape.isModelSpace) {
            // For model space normals, the full transform is passed via the view matrix.
            // Send an identity matrix here.
            shader.setMat4("model", glm::mat4(1.0f));
            shader.setBool("is_model_space", true);
            shader.setMat4("model_transform", shape.transform);
        }
        else {
            shader.setMat4("model", shape.transform);
            shader.setBool("is_model_space", false);
            shader.setMat4("model_transform", glm::mat4(1.0f)); // Not used
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
        if (shape.isModelSpace) {
            shader.setMat4("model", glm::mat4(1.0f));
            shader.setBool("is_model_space", true);
            shader.setMat4("model_transform", shape.transform);
        }
        else {
            shader.setMat4("model", shape.transform);
            shader.setBool("is_model_space", false);
            shader.setMat4("model_transform", glm::mat4(1.0f));
        }

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
