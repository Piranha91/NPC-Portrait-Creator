#include "NifModel.h"
#include "Shader.h"
#include "TextureManager.h" // Include the new texture manager
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 
#include <limits>

// Helper function to get the full world transform of a shape
nifly::MatTransform GetShapeTransformToGlobal(const nifly::NifFile& nifFile, nifly::NiShape* niShape) {
    nifly::MatTransform xform = niShape->GetTransformToParent();
    nifly::NiNode* parent = nifFile.GetParentNode(niShape);
    while (parent) {
        xform = parent->GetTransformToParent().ComposeTransforms(xform);
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

    // --- GET SHAPES FIRST ---
    const auto& shapeList = nif.GetShapes();
    // --- DEBUG BLOCK MOVED TO HERE ---
    std::cout << "\n--- NIF Block Debug Information ---\n";
    uint32_t numBlocks = nif.GetHeader().GetNumBlocks();
    for (uint32_t i = 0; i < numBlocks; ++i) {
        nifly::NiObject* block = nif.GetHeader().GetBlock<nifly::NiObject>(i);
        if (!block) continue;

        std::cout << "Block " << i << ": "
            << "Type = " << block->GetBlockName();
        if (nifly::NiAVObject* avObject = dynamic_cast<nifly::NiAVObject*>(block)) {
            std::cout << ", Name = \"" << avObject->name.get() << "\"";
            std::cout << ", Flags = " << avObject->flags;
            if (avObject->flags & 1) {
                std::cout << " (HIDDEN)";
            }
        }
        std::cout << std::endl;
    }
    std::cout << "-------------------------------------\n\n";
    // --- END OF MOVED BLOCK ---

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    if (shapeList.empty()) {
        std::cerr << "Warning: NIF file contains no shapes." << std::endl;
        modelCenter = glm::vec3(0.0f, 50.0f, 0.0f);
        modelBoundsSize = glm::vec3(300.0f);
        return true;
    }

    for (auto* niShape : shapeList) {
        // The rest of the function remains exactly the same...
        if (!niShape) continue;
        if (niShape->flags & 1) {
            continue;
        }

        if (auto* geomData = niShape->GetGeomData()) {
            if (!niShape->HasNormals()) {
                geomData->RecalcNormals();
            }
        }

        const auto* vertices = nif.GetVertsForShape(niShape);
        const auto* normals = nif.GetNormalsForShape(niShape);
        const auto* uvs = nif.GetUvsForShape(niShape);
        std::vector<nifly::Triangle> triangles;
        niShape->GetTriangles(triangles);
        if (!vertices || vertices->empty() || triangles.empty()) {
            continue;
        }

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
        mesh.transform = glm::make_mat4(&niflyTransform.ToMatrix()[0]);

        const auto shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
        }

        if (vertices) {
            if (mesh.isModelSpace) {
                for (const auto& vert : *vertices) {
                    minBounds = glm::min(minBounds, glm::vec3(vert.x, vert.y, vert.z));
                    maxBounds = glm::max(maxBounds, glm::vec3(vert.x, vert.y, vert.z));
                }
            }
            else {
                for (const auto& vert : *vertices) {
                    glm::vec4 transformedVert = mesh.transform * glm::vec4(vert.x, vert.y, vert.z, 1.0f);
                    minBounds = glm::min(minBounds, glm::vec3(transformedVert));
                    maxBounds = glm::max(maxBounds, glm::vec3(transformedVert));
                }
            }
        }

        if (shader && shader->HasTextureSet()) {
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                // Iterate through the texture paths in the set
                for (size_t i = 0; i < textureSet->textures.size(); ++i) {
                    std::string texPath = textureSet->textures[i].get(); 
                        if (texPath.empty()) {
                            continue;
                        }

                    // Load texture based on its conventional slot index
                    switch (i) {
                    case 0: // Diffuse Map
                        mesh.diffuseTextureID = textureManager.loadTexture(texPath);
                        break;
                    case 1: // Normal Map
                        mesh.normalTextureID = textureManager.loadTexture(texPath);
                        break;
                    case 2: // Skin/Subsurface Map (_sk)
                        mesh.skinTextureID = textureManager.loadTexture(texPath);
                        break;
                    case 3: // Detail Map
                        mesh.detailTextureID = textureManager.loadTexture(texPath);
                        break;
                    case 7: // Specular Map
                        mesh.specularTextureID = textureManager.loadTexture(texPath);
                        break;
                    default:
                        break;
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

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

        glBindVertexArray(0);

        shapes.push_back(mesh);
    }

    modelCenter = (minBounds + maxBounds) * 0.5f;
    modelBoundsSize = maxBounds - minBounds;
    std::cout << "Successfully loaded " << shapes.size() << " shapes from NIF." << std::endl;
    return true;
}

void NifModel::draw(Shader& shader) {
    shader.use();
    // Set the texture samplers to their corresponding texture units
    shader.setInt("texture_diffuse1", 0);
    shader.setInt("texture_normal", 1);
    shader.setInt("texture_skin", 2);
    shader.setInt("texture_detail", 3);
    shader.setInt("texture_specular", 4);

    for (const auto& shape : shapes) {
        if (shape.isModelSpace) {
            // This mesh is already in model space, so don't apply the model transform.
            // Pass an identity matrix instead.
            shader.setMat4("model", glm::mat4(1.0f));
        }
        else {
            // For regular meshes, apply the calculated world transform.
            shader.setMat4("model", shape.transform);
        }

        // Bind Diffuse Map (Texture Unit 0)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);

        // Bind Normal Map (Texture Unit 1)
        shader.setBool("has_normal_map", shape.normalTextureID != 0);
        if (shape.normalTextureID != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shape.normalTextureID);
        }

        // Bind Skin Map (Texture Unit 2)
        shader.setBool("has_skin_map", shape.skinTextureID != 0);
        if (shape.skinTextureID != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, shape.skinTextureID);
        }

        // Bind Detail Map (Texture Unit 3)
        shader.setBool("has_detail_map", shape.detailTextureID != 0);
        if (shape.detailTextureID != 0) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, shape.detailTextureID);
        }

        // Bind Specular Map (Texture Unit 4)
        shader.setBool("has_specular_map", shape.specularTextureID != 0);
        if (shape.specularTextureID != 0) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, shape.specularTextureID);
        }

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