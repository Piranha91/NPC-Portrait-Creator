#include "NifModel.h"
#include "Shader.h"
#include "TextureManager.h" // Include the new texture manager
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 

// Helper function to get the full world transform of a shape
nifly::MatTransform GetShapeTransformToGlobal(const nifly::NifFile& nifFile, nifly::NiShape* niShape) {
    nifly::MatTransform xform = niShape->transform;
    nifly::NiNode* parent = nifFile.GetParentNode(niShape);
    while (parent) {
        xform = parent->transform.ComposeTransforms(xform);
        parent = nifFile.GetParentNode(parent);
    }
    return xform;
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
    cleanup();
    if (nif.Load(nifPath) != 0) {
        std::cerr << "Error: Failed to load NIF file: " << nifPath << std::endl;
        return false;
    }

    // This section can be used for debug logging if you wish
    std::set<std::string> uniqueTexturePaths;
    for (const auto& shape : nif.GetShapes()) {
        const auto shader = nif.GetShader(shape);
        if (shader && shader->HasTextureSet()) {
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                for (const auto& tex : textureSet->textures) {
                    if (auto path = tex.get(); !path.empty()) {
                        uniqueTexturePaths.insert(path);
                    }
                }
            }
        }
    }
    texturePaths.assign(uniqueTexturePaths.begin(), uniqueTexturePaths.end());

    const auto& shapeList = nif.GetShapes();
    if (shapeList.empty()) {
        std::cerr << "Warning: NIF file contains no shapes." << std::endl;
        return true;
    }

    for (auto* niShape : shapeList) {
        if (!niShape) continue;
        if (auto* geomData = niShape->GetGeomData()) {
            if (!niShape->HasNormals()) {
                geomData->RecalcNormals();
            }
        }

        const auto* vertices = nif.GetVertsForShape(niShape);
        const auto* normals = nif.GetNormalsForShape(niShape);
        const auto* uvs = nif.GetUvsForShape(niShape); // Get UVs
        std::vector<nifly::Triangle> triangles;
        niShape->GetTriangles(triangles);
        if (!vertices || vertices->empty() || triangles.empty()) {
            continue;
        }

        // Updated Vertex struct to include texture coordinates
        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
            glm::vec2 texCoords;
        };

        std::vector<Vertex> vertexData;
        vertexData.resize(vertices->size());
        for (size_t i = 0; i < vertices->size(); ++i) {
            vertexData[i].pos = glm::vec3((*vertices)[i].x, (*vertices)[i].y, (*vertices)[i].z);
            if (normals && i < normals->size()) {
                vertexData[i].normal = glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
            }
            else {
                vertexData[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (uvs && i < uvs->size()) {
                // DDS files often have their origin at the top-left, matching OpenGL, so no flip is needed.
                // If textures appear upside down, use: glm::vec2((*uvs)[i].u, 1.0f - (*uvs)[i].v);
                vertexData[i].texCoords = glm::vec2((*uvs)[i].u, (*uvs)[i].v);
            }
            else {
                vertexData[i].texCoords = glm::vec2(0.0f, 0.0f);
            }
        }

        std::vector<unsigned short> indices;
        indices.reserve(triangles.size() * 3);
        for (const auto& tri : triangles) {
            indices.push_back(tri.p1);
            indices.push_back(tri.p2);
            indices.push_back(tri.p3);
        }

        MeshShape mesh;
        mesh.indexCount = static_cast<GLsizei>(indices.size());

        auto niflyTransform = GetShapeTransformToGlobal(nif, niShape);
        auto matData = niflyTransform.ToMatrix();
        mesh.transform = glm::transpose(glm::make_mat4(&matData[0]));

        // Load texture for this shape using the TextureManager
        const auto shader = nif.GetShader(niShape);
        if (shader && shader->HasTextureSet()) {
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                if (!textureSet->textures.empty()) {
                    std::string texPath = textureSet->textures[0].get(); // Get diffuse texture
                    if (!texPath.empty()) {
                        mesh.diffuseTextureID = textureManager.loadTexture(texPath);
                    }
                }
            }
        }

        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);

        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(Vertex), vertexData.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned short), indices.data(), GL_STATIC_DRAW);

        // Position Attribute (location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

        // Normal Attribute (location = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

        // Texture Coordinate Attribute (location = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

        glBindVertexArray(0);

        shapes.push_back(mesh);
    }

    std::cout << "Successfully loaded " << shapes.size() << " shapes from NIF." << std::endl;
    return true;
}

void NifModel::draw(Shader& shader) {
    shader.use();
    // Tell the shader that the `texture_diffuse1` sampler should use texture unit 0
    shader.setInt("texture_diffuse1", 0);

    for (const auto& shape : shapes) {
        shader.setMat4("model", shape.transform);

        // Activate texture unit 0 and bind the correct texture for this shape
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);

        shape.draw();
    }
}

void NifModel::cleanup() {
    for (auto& shape : shapes) {
        shape.cleanup();
    }
    shapes.clear();
    texturePaths.clear();
}

std::vector<std::string> NifModel::getTextures() const {
    return texturePaths;
}