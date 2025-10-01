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

struct MeshShape {
    std::string name;
    bool visible = true;
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLsizei indexCount = 0;

    // This matrix transforms a vertex from this shape's local model space
    // into the NIF file's overall root coordinate space.
    //
    // Input Space: Shape's Local Model Space (Z-up)
    // Transformation: Applies the shape's translation, rotation, and scale relative to the NIF root.
    // Output Space: NIF Root Space (Z-up)
    glm::mat4 shapeLocalToNifRoot_transform_zUp = glm::mat4(1.0f);

    // Stores the world-space center of the mesh's bounding box. This is calculated
    // in the NIF's root coordinate space.
    //
    // Coordinate Space: NIF Root Space (Z-up)
    glm::vec3 boundsCenter_nifRootSpace_zUp = glm::vec3(0.0f);

    GLuint diffuseTextureID = 0; // Slot 0: _d.dds or base color
    GLuint normalTextureID = 0; // Slot 1: _n.dds or _msn.dds
    GLuint skinTextureID = 0; // Slot 2: _sk.dds (Subsurface/Tint)
    GLuint detailTextureID = 0; // Slot 3: _detail.dds
    GLuint faceTintColorMaskID = 0; // Slot 6: Face Tint Mask
    GLuint specularTextureID = 0; // Slot 7: _s.dds
    bool isModelSpace = false;
    bool isEye = false; // NEW: Flag for eye-specific shader logic

    // --- Additions for GPU Skinning ---
    bool isSkinned = false;
    // A collection of matrices for GPU skinning. Each matrix transforms a vertex from the
    // mesh's initial bind-pose model space to a bone's final animated pose space.
    //
    // Input Space: Mesh's Bind-Pose Model Space (Z-up)
    // Transformation: Applies the combined inverse-bind and final bone animation transform.
    // Output Space: Bone's Posed Space (Z-up)
    std::vector<glm::mat4> skinToBonePose_transforms_zUp;

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
    float materialAlpha = 1.0f;
    // --- Additions for Tint
    bool hasTintColor = false;
    glm::vec3 tintColor = glm::vec3(1.0f); // Default to white (no tint)

    // --- Shader Flags
    bool hasSpecularFlag = false; // To store the state of the SLSF1_Specular flag
    bool hasEnvMapFlag = false;
    bool hasEyeEnvMapFlag = false;
    bool receiveShadows = false;
    bool castShadows = false;
    bool hasOwnEmitFlag = false;
    bool has_specular_map = false; // Whether a specular map is assigned

    // --- Shader Properties
    GLuint environmentMapID = 0;   // Slot 4: _e.dds or _em.dds
    GLenum environmentMapTarget = GL_TEXTURE_2D;
    GLuint environmentMaskID = 0;  // Slot 5: _m.dds
    float envMapScale = 1.0f;
    glm::vec3 emissiveColor = glm::vec3(0.0f);
    float emissiveMultiple = 1.0f;

    void draw() const;
    void cleanup();
};

class NifModel {
public:
    NifModel();
    ~NifModel();

    bool load(const std::string& path, TextureManager& textureManager, const Skeleton* skeleton);
    bool load(const std::vector<char>& data, const std::string& nifPath, TextureManager& textureManager, const Skeleton* skeleton);
    void draw(Shader& shader, const glm::vec3& cameraPos, const glm::mat4& nifRootToWorld_conversionMatrix_zUpToYUp);
    void drawDepthOnly(Shader& depthShader);
    void cleanup();

    std::vector<MeshShape>& getOpaqueShapes() { return opaqueShapes; }
    std::vector<MeshShape>& getAlphaTestShapes() { return alphaTestShapes; }
    std::vector<MeshShape>& getTransparentShapes() { return transparentShapes; }

    std::vector<std::string> getTextures() const;

    // --- Accessors for Bounds ---
    // Note: All bounds are stored in the NIF file's root coordinate space, which is Z-up.
    // They must be transformed by a conversion matrix to be used in the Y-up world space of the renderer.

    glm::vec3 getMinBounds_nifRootSpace_zUp() const { return minBounds_nifRootSpace_zUp; }
    glm::vec3 getMaxBounds_nifRootSpace_zUp() const { return maxBounds_nifRootSpace_zUp; }
    // The aggregate bounds, calculated by excluding accessories by name
    glm::vec3 getHeadMinBounds_nifRootSpace_zUp() const { return headMinBounds_nifRootSpace_zUp; }
    glm::vec3 getHeadMaxBounds_nifRootSpace_zUp() const { return headMaxBounds_nifRootSpace_zUp; }

    // The specific bounds of the shape with the SBP_HEAD partition, if found
    glm::vec3 getHeadShapeMinBounds_nifRootSpace_zUp() const { return headShapeMinBounds_nifRootSpace_zUp; }
    glm::vec3 getHeadShapeMaxBounds_nifRootSpace_zUp() const { return headShapeMaxBounds_nifRootSpace_zUp; }
    bool hasHeadShapeBounds() const { return bHasHeadShapeBounds; }
    glm::vec3 getEyeCenter_nifRootSpace_zUp() const { return eyeCenter_nifRootSpace_zUp; }
    bool hasEyeCenter() const { return bHasEyeCenter; }
    glm::vec3 getCenter_nifRootSpace_zUp() const { return (minBounds_nifRootSpace_zUp + maxBounds_nifRootSpace_zUp) * 0.5f; }
    glm::vec3 getBoundsSize_nifRootSpace_zUp() const { return maxBounds_nifRootSpace_zUp - minBounds_nifRootSpace_zUp; }

private:
    nifly::NifFile nif;
    std::vector<MeshShape> opaqueShapes;
    std::vector<MeshShape> alphaTestShapes;
    std::vector<MeshShape> transparentShapes;
    std::vector<std::string> texturePaths;

    // --- Bounding Box Members ---
    // These define the AABB for the entire model in the NIF's root coordinate space.
    // Coordinate Space: NIF Root Space (Z-up)
    glm::vec3 minBounds_nifRootSpace_zUp;
    glm::vec3 maxBounds_nifRootSpace_zUp;

    // AABB for the head parts of the model, excluding accessories.
    // Coordinate Space: NIF Root Space (Z-up)
    glm::vec3 headMinBounds_nifRootSpace_zUp;
    glm::vec3 headMaxBounds_nifRootSpace_zUp;

    // AABB for the specific shape identified as the primary head mesh via dismemberment partitions.
    // Coordinate Space: NIF Root Space (Z-up)
    glm::vec3 headShapeMinBounds_nifRootSpace_zUp;
    glm::vec3 headShapeMaxBounds_nifRootSpace_zUp;
    bool bHasHeadShapeBounds = false;

    // Geometric center of the eye mesh(es).
    // Coordinate Space: NIF Root Space (Z-up)
    glm::vec3 eyeCenter_nifRootSpace_zUp;
    bool bHasEyeCenter = false;
};