#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>
#include <memory>

#include <NifFile.hpp>

// Forward-declare classes to avoid circular dependencies
class Shader;
class TextureManager;

struct MeshShape {
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLsizei indexCount = 0;
    glm::mat4 transform = glm::mat4(1.0f); // Initialize to identity matrix
    GLuint diffuseTextureID = 0; // Texture ID for the diffuse map
    bool isModelSpace = false;

    void draw() const;
    void cleanup();
};


class NifModel {
public:
    NifModel();
    ~NifModel();

    // Updated signature to accept a TextureManager
    bool load(const std::string& path, TextureManager& textureManager);
    void draw(Shader& shader);
    void cleanup();

    // This is no longer used by the renderer for loading but can be kept for debugging
    std::vector<std::string> getTextures() const;

    glm::vec3 getCenter() const { return modelCenter; }
    float getSize() const { return modelSize; }

private:
    nifly::NifFile nif;
    std::vector<MeshShape> shapes;
    std::vector<std::string> texturePaths; // Stores texture paths for debugging
    glm::vec3 modelCenter = glm::vec3(0.0f);
    float modelSize = 0.0f;
};