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
    GLuint diffuseTextureID = 0;           // Slot 0: _d.dds or base color
    GLuint normalTextureID = 0;            // Slot 1: _n.dds or _msn.dds
    GLuint skinTextureID = 0;              // Slot 2: _sk.dds (Subsurface/Tint)
    GLuint detailTextureID = 0;            // Slot 3: _detail.dds
    GLuint specularTextureID = 0;          // Slot 7: _s.dds
    bool isModelSpace = false;

    // --- Additions for Alpha Properties ---
    bool hasAlphaProperty = false;
    bool alphaBlend = false;
    bool alphaTest = false;
    float alphaThreshold = 0.5f;
    GLenum srcBlend = GL_SRC_ALPHA;
    GLenum dstBlend = GL_ONE_MINUS_SRC_ALPHA;

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
    glm::vec3 getBoundsSize() const { return modelBoundsSize; }

private:
    nifly::NifFile nif;
    std::vector<MeshShape> shapes;
    std::vector<std::string> texturePaths; // Stores texture paths for debugging
    glm::vec3 modelCenter = glm::vec3(0.0f);
    glm::vec3 modelBoundsSize = glm::vec3(0.0f);
};