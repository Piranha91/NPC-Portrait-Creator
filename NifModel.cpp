#include "NifModel.h"
#include "Shader.h" // Include Shader header for the draw function
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 

// Helper function to get the full world transform of a shape,
// similar to the original repository's GetShapeTransformToGlobal function.
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

bool NifModel::load(const std::string& nifPath) {
    cleanup();

    if (nif.Load(nifPath) != 0) {
        std::cerr << "Error: Failed to load NIF file: " << nifPath << std::endl;
        return false;
    }

    // --- Texture Extraction using reference syntax ---
    std::set<std::string> uniqueTexturePaths;
    for (const auto& shape : nif.GetShapes()) {
        const auto shader = nif.GetShader(shape);
        if (shader && shader->HasTextureSet()) {
            const auto textureSetRef = shader->TextureSetRef();

            // FIXED: Use the templated GetBlock<T>() function which handles casting internally.
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(textureSetRef)) {
                for (const auto& tex : textureSet->textures) {
                    if (auto path = tex.get(); !path.empty()) {
                        uniqueTexturePaths.insert(path);
                    }
                }
            }
        }
    }
    // Copy unique paths from the set to our member vector
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

        std::vector<nifly::Triangle> triangles;

        const auto* vertices = nif.GetVertsForShape(niShape);
        const auto* normals = nif.GetNormalsForShape(niShape);
        niShape->GetTriangles(triangles);

        if (!vertices || vertices->empty() || triangles.empty()) {
            continue;
        }

        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
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

        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);

        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(Vertex), vertexData.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned short), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

        glBindVertexArray(0);

        shapes.push_back(mesh);
    }

    std::cout << "Successfully loaded " << shapes.size() << " shapes from NIF." << std::endl;
    return true;
}

void NifModel::draw(Shader& shader) {
    for (const auto& shape : shapes) {
        shader.setMat4("model", shape.transform);
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