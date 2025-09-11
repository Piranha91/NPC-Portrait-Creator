// NifModel.h

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
class Skeleton;

// Vertex structure used for processing mesh data
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec4 color;
    glm::vec4 tangent; // xyz = tangent, w = handedness
};

struct MeshShape {
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLsizei indexCount = 0;
    glm::mat4 transform = glm::mat4(1.0f); // Initialize to identity matrix
    GLuint diffuseTextureID = 0; // Slot 0: _d.dds or base color
    GLuint normalTextureID = 0; // Slot 1: _n.dds or _msn.dds
    GLuint skinTextureID = 0; // Slot 2: _sk.dds (Subsurface/Tint)
    GLuint detailTextureID = 0; // Slot 3: _detail.dds
    GLuint faceTintColorMaskID = 0; // Slot 6: Face Tint Mask
    GLuint specularTextureID = 0; // Slot 7: _s.dds
    bool isModelSpace = false;
    // --- Additions for Alpha Properties ---
    bool hasAlphaProperty = false;
    bool alphaBlend = false;
    bool alphaTest = false;
    float alphaThreshold = 0.5f;
    GLenum srcBlend = GL_SRC_ALPHA;
    GLenum dstBlend = GL_ONE_MINUS_SRC_ALPHA;
    // --- Additions for Material Properties ---
    bool doubleSided = false;
    bool zBufferWrite = true;
    // --- Additions for Tint
    bool hasTintColor = false;
    glm::vec3 tintColor = glm::vec3(1.0f); // Default to white (no tint)

    void draw() const;
    void cleanup();
};



class NifModel {
public:
    NifModel();
    ~NifModel();

    bool load(const std::string& path, TextureManager& textureManager, const Skeleton* skeleton);
    void draw(Shader& shader, const glm::vec3& cameraPos);
    void cleanup();

    std::vector<std::string> getTextures() const;

    // --- Restored Accessors from Version 1 ---
    glm::vec3 getMinBounds() const { return minBounds; }
    glm::vec3 getMaxBounds() const { return maxBounds; }
    glm::vec3 getHeadMinBounds() const { return headMinBounds; }
    glm::vec3 getHeadMaxBounds() const { return headMaxBounds; }
    glm::vec3 getEyeCenter() const { return eyeCenter; }
    bool hasEyeCenter() const { return bHasEyeCenter; }
    glm::vec3 getCenter() const { return (minBounds + maxBounds) * 0.5f; }
    glm::vec3 getBoundsSize() const { return maxBounds - minBounds; }

private:
    nifly::NifFile nif;
    std::vector<MeshShape> opaqueShapes;
    std::vector<MeshShape> alphaTestShapes;
    std::vector<MeshShape> transparentShapes;
    std::vector<std::string> texturePaths;

    // --- Restored Members from Version 1 ---
    glm::vec3 minBounds;
    glm::vec3 maxBounds;
    glm::vec3 headMinBounds;
    glm::vec3 headMaxBounds;
    glm::vec3 eyeCenter;
    bool bHasEyeCenter = false;
};