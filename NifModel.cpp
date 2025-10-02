#define GLM_ENABLE_EXPERIMENTAL
#define MAX_BONES 80 // <-- MAKE SURE THIS MATCHES YOUR SHADER'S ARRAY SIZE

#include "NifModel.h"
#include "Skeleton.h"
#include "Shader.h"
#include "TextureManager.h" 
#include "Renderer.h"
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 
#include <limits>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/norm.hpp>  // gives length2() and distance2()
#include <chrono>
#include <sstream> // Add for std::stringstream
#include <fstream> 

// Vertex structure used for processing mesh data, now includes skinning and tangent space info
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec4 color;
    glm::ivec4 boneIDs = glm::ivec4(0);
    glm::vec4 weights = glm::vec4(0.0f);
    glm::vec3 tangent = glm::vec3(0.0f);
    glm::vec3 bitangent = glm::vec3(0.0f);
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

struct ShaderFlagSet {
    // Flags from shaderFlags1 (SLSF1)
    bool SLSF1_Specular = false;
    bool SLSF1_Skinned = false;
    bool SLSF1_Environment_Mapping = false;
    bool SLSF1_Hair_Soft_Lighting = false;
    bool SLSF1_Receive_Shadows = false;
    bool SLSF1_Cast_Shadows = false;
    bool SLSF1_Eye_Environment_Mapping = false;
    bool SLSF1_Decal = false;
    bool SLSF1_Own_Emit = false;
    bool SLSF1_Vertex_Alpha = false;
    bool SLSF1_Model_Space_Normals = false;
    bool SLSF1_FaceGen_Detail_Map = false;

    // Flags from shaderFlags2 (SLSF2)
    bool SLSF2_ZBuffer_Write = false;
    bool SLSF2_Packed_Tangent = false;
    bool SLSF2_Double_Sided = false;
    bool SLSF2_Remappable_Textures = false;
    bool SLSF2_Vertex_Colors = false;
    bool SLSF2_Assume_Shadowmask = false;
    bool SLSF2_Soft_Lighting = false;
    bool SLSF2_EnvMap_Light_Fade = false;
};

// Parses the raw integer shader flags into a structured ShaderFlagSet.
ShaderFlagSet ParseShaderFlags(uint32_t shaderFlags1, uint32_t shaderFlags2) {
    ShaderFlagSet flags;

    // --- Parse shaderFlags1 ---
    flags.SLSF1_Specular = (shaderFlags1 >> 0) & 1;
    flags.SLSF1_Skinned = (shaderFlags1 >> 1) & 1;
    flags.SLSF1_Environment_Mapping = (shaderFlags1 >> 2) & 1;
    flags.SLSF1_Hair_Soft_Lighting = (shaderFlags1 >> 3) & 1;
    flags.SLSF1_Receive_Shadows = (shaderFlags1 >> 7) & 1;
    flags.SLSF1_Cast_Shadows = (shaderFlags1 >> 8) & 1;
    flags.SLSF1_Eye_Environment_Mapping = (shaderFlags1 >> 10) & 1;
    flags.SLSF1_Decal = (shaderFlags1 >> 11) & 1;
    flags.SLSF1_Own_Emit = (shaderFlags1 >> 14) & 1;
    flags.SLSF1_Vertex_Alpha = (shaderFlags1 >> 24) & 1;
    flags.SLSF1_Model_Space_Normals = (shaderFlags1 >> 28) & 1;
    flags.SLSF1_FaceGen_Detail_Map = (shaderFlags1 >> 30) & 1;

    // --- Parse shaderFlags2 ---
    flags.SLSF2_ZBuffer_Write = (shaderFlags2 >> 0) & 1;
    flags.SLSF2_Packed_Tangent = (shaderFlags2 >> 1) & 1;
    flags.SLSF2_Double_Sided = (shaderFlags2 >> 4) & 1;
    flags.SLSF2_Remappable_Textures = (shaderFlags2 >> 5) & 1;
    flags.SLSF2_Vertex_Colors = (shaderFlags2 >> 7) & 1;
    flags.SLSF2_Assume_Shadowmask = (shaderFlags2 >> 10) & 1;
    flags.SLSF2_Soft_Lighting = (shaderFlags2 >> 13) & 1;
    flags.SLSF2_EnvMap_Light_Fade = (shaderFlags2 >> 25) & 1;

    return flags;
}

// Helper function to decode a 32-bit integer of shader flags into a string
std::string GetFlagsString(const ShaderFlagSet& flags, int set_number) {
    std::stringstream ss;
    bool first = true;
    auto add_flag = [&](const std::string& name) {
        if (!first) ss << " | ";
        ss << name;
        first = false;
        };

    if (set_number == 1) { // Stringify SLSF1 flags
        if (flags.SLSF1_Specular)              add_flag("SLSF1_Specular");
        if (flags.SLSF1_Skinned)               add_flag("SLSF1_Skinned");
        if (flags.SLSF1_Environment_Mapping)   add_flag("SLSF1_Environment_Mapping");
        if (flags.SLSF1_Hair_Soft_Lighting)    add_flag("SLSF1_Hair_Soft_Lighting");
        if (flags.SLSF1_Receive_Shadows)       add_flag("SLSF1_Receive_Shadows");
        if (flags.SLSF1_Cast_Shadows)          add_flag("SLSF1_Cast_Shadows");
        if (flags.SLSF1_Eye_Environment_Mapping) add_flag("SLSF1_Eye_Environment_Mapping");
        if (flags.SLSF1_Decal)                 add_flag("SLSF1_Decal");
        if (flags.SLSF1_Own_Emit)              add_flag("SLSF1_Own_Emit");
        if (flags.SLSF1_Vertex_Alpha)          add_flag("SLSF1_Vertex_Alpha");
        if (flags.SLSF1_Model_Space_Normals)   add_flag("SLSF1_Model_Space_Normals");
        if (flags.SLSF1_FaceGen_Detail_Map)    add_flag("SLSF1_FaceGen_Detail_Map");
    }
    else { // Stringify SLSF2 flags
        if (flags.SLSF2_ZBuffer_Write)         add_flag("SLSF2_ZBuffer_Write");
        if (flags.SLSF2_Packed_Tangent)        add_flag("SLSF2_Packed_Tangent");
        if (flags.SLSF2_Double_Sided)          add_flag("SLSF2_Double_Sided");
        if (flags.SLSF2_Remappable_Textures)   add_flag("SLSF2_Remappable_Textures");
        if (flags.SLSF2_Vertex_Colors)         add_flag("SLSF2_Vertex_Colors");
        if (flags.SLSF2_Assume_Shadowmask)     add_flag("SLSF2_Assume_Shadowmask");
        if (flags.SLSF2_Soft_Lighting)         add_flag("SLSF2_Soft_Lighting");
        if (flags.SLSF2_EnvMap_Light_Fade)     add_flag("SLSF2_EnvMap_Light_Fade");
    }
    return ss.str();
}

// Helper function to get the full world transform of any scene graph object (Node or Shape).
// This calculates the transformation from the object's local space to the NIF file's root space.
nifly::MatTransform GetAVObjectTransformToGlobal(const nifly::NifFile& nifFile, nifly::NiAVObject* obj, bool debugMode = false) {
    if (!obj) {
        return nifly::MatTransform();
    }

    // This matrix transforms from the object's local space to its parent's space.
    // It is a row-major matrix within the NIF's Z-up coordinate system.
    nifly::MatTransform objectToNifRoot_transform_zUp = obj->GetTransformToParent();

    nifly::NiNode* parent = nifFile.GetParentNode(obj);
    while (parent) {
        // ComposeTransforms pre-multiplies, so the operation is: parent * current.
        // This correctly concatenates the transforms up the scene graph hierarchy.
        objectToNifRoot_transform_zUp = parent->GetTransformToParent().ComposeTransforms(objectToNifRoot_transform_zUp);

        // --- CORRECTED LOGGING ---
        if (debugMode) {
            std::stringstream niflyMatrixStream;
            // --- MODIFIED LINE ---
            const float* matrixData = &objectToNifRoot_transform_zUp.ToMatrix()[0]; // Get a pointer to the data
            niflyMatrixStream << "\n";
            for (int row = 0; row < 4; ++row) {
                niflyMatrixStream << "        [";
                for (int col = 0; col < 4; ++col) {
                    // Access data in row-major order and format it
                    niflyMatrixStream << std::setw(9) << std::fixed << std::setprecision(4) << matrixData[row * 4 + col];
                    if (col < 3) niflyMatrixStream << ", ";
                }
                niflyMatrixStream << "]\n";
            }
            std::cout << "  [GetAVObjectTransformToGlobal] Composed with parent '" << parent->name.get() << "'. Cumulative Matrix (Nifly Z-up, Row-major):" << niflyMatrixStream.str();
        }
        // --- END CORRECTION ---

        parent = nifFile.GetParentNode(parent);
    }
    return objectToNifRoot_transform_zUp;
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

// --- HELPER FUNCTIONS FOR SKELETON ROOT FINDING ---

// Checks if a dismemberment partition ID corresponds to a known head slot.
static bool isHeadDismemberPartition(int partID) {
    // SBP_30_HEAD, SBP_130_HEAD (often for circlets/hair), SBP_230_HEAD (beast)
    return partID == 30 || partID == 130 || partID == 230;
}


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

bool NifModel::load(const std::vector<char>& data, const std::string& nifPath, TextureManager& textureManager, const Skeleton* skeleton) {
    cleanup();
    bool debugMode = true; // Set to true to enable debug output

    std::stringstream nifStream(std::string(data.begin(), data.end()));
    if (nif.Load(nifStream) != 0) { // <-- Load from memory stream
        std::cerr << "Error: Failed to load NIF from memory: " << nifPath << std::endl;
        return false;
    }

    const auto& shapeList = nif.GetShapes();
    if (shapeList.empty()) {
        std::cerr << "Warning: NIF file contains no shapes." << std::endl;
        return true;
    }

    // Reset bounds for the new model. These are calculated in the NIF's Z-up root space.
    minBounds_nifRootSpace_zUp = glm::vec3(std::numeric_limits<float>::max());
    maxBounds_nifRootSpace_zUp = glm::vec3(std::numeric_limits<float>::lowest());
    if (debugMode) {
        std::cout << "[Bounds] Initial Min Bounds: (" << minBounds_nifRootSpace_zUp.x << ", " << minBounds_nifRootSpace_zUp.y << ", " << minBounds_nifRootSpace_zUp.z << ")\n";
        std::cout << "[Bounds] Initial Max Bounds: (" << maxBounds_nifRootSpace_zUp.x << ", " << maxBounds_nifRootSpace_zUp.y << ", " << maxBounds_nifRootSpace_zUp.z << ")\n";
    }
    headMinBounds_nifRootSpace_zUp = glm::vec3(std::numeric_limits<float>::max());
    headMaxBounds_nifRootSpace_zUp = glm::vec3(std::numeric_limits<float>::lowest());

    // --- MODIFICATION: Initialize new head shape bound members ---
    headShapeMinBounds_nifRootSpace_zUp = glm::vec3(std::numeric_limits<float>::max());
    headShapeMaxBounds_nifRootSpace_zUp = glm::vec3(std::numeric_limits<float>::lowest());
    bHasHeadShapeBounds = false;

    bHasEyeCenter = false;

    // --- Stage 0: Pre-computation of Head and Accessory Transforms ---
    // Before processing shapes individually, we perform a pre-pass to identify the
    // "primary head" mesh. A NIF can have multiple shapes using a head partition
    // (e.g., face, hair, circlets). We use a heuristic (tallest mesh in local Z) to
    // find the main face mesh. The transform of this primary head is then used as a
    // reliable anchor for positioning accessories (like hair or eyes) that might have
    // an identity transform in the file.

    struct HeadCandidateInfo {
        const nifly::NiShape* shape = nullptr;
        float localHeight = 0.0f;
    };

    std::vector<HeadCandidateInfo> headCandidateInfos;
    if (debugMode) std::cout << "[NIF Analysis] Searching for primary head shape candidates...\n";
    for (auto* shape : shapeList) {
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
            bool isCandidate = false;
            for (const auto& partition : skinInst->partitions) {
                if (isHeadDismemberPartition(partition.partID)) {
                    isCandidate = true;
                    break;
                }
            }

            if (isCandidate) {
                const auto* vertices = nif.GetVertsForShape(shape);
                if (vertices && !vertices->empty()) {
                    float minZ = std::numeric_limits<float>::max();
                    float maxZ = std::numeric_limits<float>::lowest();
                    for (const auto& v : *vertices) {
                        minZ = std::min(minZ, v.z);
                        maxZ = std::max(maxZ, v.z);
                    }
                    headCandidateInfos.push_back({ shape, maxZ - minZ });
                    if (debugMode) std::cout << "    -> Found candidate: '" << shape->name.get() << "' with local height " << (maxZ - minZ) << ".\n";
                }
            }
        }
    }

    std::string primaryHeadShapeName;
    const nifly::NiShape* primaryHeadShape = nullptr;
    if (!headCandidateInfos.empty()) {
        auto tallestIt = std::max_element(headCandidateInfos.begin(), headCandidateInfos.end(),
            [](const HeadCandidateInfo& a, const HeadCandidateInfo& b) {
                return a.localHeight < b.localHeight;
            });

        if (tallestIt != headCandidateInfos.end()) {
            primaryHeadShape = tallestIt->shape;
            primaryHeadShapeName = primaryHeadShape->name.get();
            if (debugMode) {
                std::cout << "[NIF Analysis] Selected primary head shape: '" << primaryHeadShapeName << "'.\n";
            }
        }
    }
    if (primaryHeadShapeName.empty() && debugMode) {
        std::cout << "[NIF Analysis] Warning: Could not identify a primary head shape from candidates.\n";
    }

    // --- HEURISTIC TO DETECT NIF TYPE (Standard vs. Hybrid) ---
    // This logic is crucial for handling different ways NIF files are authored.
    bool isHybridModel = false;
    const float PRETRANSLATED_THRESHOLD = 10.0f;
    for (auto* shape : shapeList) {
        if (!shape->IsSkinned()) continue;
        // This is the shape's transform from its local space to the NIF root space (Z-up).
        nifly::MatTransform shapeToNifRoot_transform_zUp_nifly = GetAVObjectTransformToGlobal(nif, shape, false);
        // A hybrid model has skinned parts with no transform, but their vertices are far from the origin.
        if (shapeToNifRoot_transform_zUp_nifly.IsNearlyEqualTo(nifly::MatTransform())) {
            const auto* v = nif.GetVertsForShape(shape);
            if (v && !v->empty()) {
                glm::vec3 sum(0.0f); for (const auto& vert : *v) sum += glm::vec3(vert.x, vert.y, vert.z);
                glm::vec3 centroid = sum / static_cast<float>(v->size());
                if (glm::length(centroid) > PRETRANSLATED_THRESHOLD) {
                    isHybridModel = true;
                    if (debugMode) std::cout << "[NIF Analysis] Hybrid model with pre-translated parts detected.\n";
                    break;
                }
            }
        }
    }

found_head:

    // This transform is used to correctly position local-space accessories like hair or eyes
    // relative to the main head mesh. It is in the NIF's Z-up root space.
    nifly::MatTransform accessoryToNifRoot_offset_zUp_nifly;
    if (primaryHeadShape) {
        accessoryToNifRoot_offset_zUp_nifly = GetAVObjectTransformToGlobal(nif, const_cast<nifly::NiShape*>(primaryHeadShape), false);
        if (debugMode) std::cout << "[NIF Analysis] Using transform from primary head '" << primaryHeadShapeName << "' as the accessory offset.\n";
    }
    else {
        // Fallback to original logic if no primary head was found via heuristic.
        if (debugMode) std::cout << "[NIF Analysis] Warning: No primary head identified. Falling back to first-found head partition for accessory offset.\n";
        for (auto* shape : shapeList) {
            if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
                bool found = false;
                for (const auto& partition : skinInst->partitions) {
                    if (isHeadDismemberPartition(partition.partID)) {
                        accessoryToNifRoot_offset_zUp_nifly = GetAVObjectTransformToGlobal(nif, shape, false);
                        if (debugMode) std::cout << "[NIF Analysis] Using fallback accessory offset from shape '" << shape->name.get() << "'.\n";
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }
    }

    // Find the skeleton root's transform, which is needed for some hybrid NIFs.
    // This represents the skeleton's root node's position and orientation within the NIF's Z-up root space.
    nifly::MatTransform skeletonRootToNifRoot_transform_zUp_nifly; // Default constructs to identity
    for (auto* shape : shapeList) {
        if (shape->IsSkinned()) {
            if (auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(shape->SkinInstanceRef())) {
                auto* skelRootNode = FindSkeletonRootLCA(nif, skinInst);
                if (skelRootNode) {
                    skeletonRootToNifRoot_transform_zUp_nifly = GetAVObjectTransformToGlobal(nif, skelRootNode);
                    if (debugMode) std::cout << "[NIF Analysis] Found skeleton root node: " << skelRootNode->name.get() << "\n";
                }
            }
            break; // We only need to find it once.
        }
    }

    for (auto* niShape : shapeList) {
        auto start_preprocess = std::chrono::high_resolution_clock::now();

        if (debugMode) std::cout << "\n--- Processing Shape: " << niShape->name.get() << " ---\n";
        if (!niShape || (niShape->flags & 1)) continue;

        // --- Stage 1: Nifly Vertex/Property Parsing ---
        auto start_stage1 = std::chrono::high_resolution_clock::now();
        const auto* vertices = nif.GetVertsForShape(niShape);
        if (!vertices || vertices->empty()) continue;

        const auto* colors = nif.GetColorsForShape(niShape->name.get());
        const auto* normals = nif.GetNormalsForShape(niShape);
        const auto* uvs = nif.GetUvsForShape(niShape);
        const auto* tangents = nif.GetTangentsForShape(niShape);
        const auto* bitangents = nif.GetBitangentsForShape(niShape);
        auto end_stage1 = std::chrono::high_resolution_clock::now();

        // --- Stage 2: Copying Data to Local Buffers ---
        auto start_stage2 = std::chrono::high_resolution_clock::now();
        std::vector<Vertex> vertexData(vertices->size());
        for (size_t i = 0; i < vertices->size(); ++i) {
            vertexData[i].pos = glm::vec3((*vertices)[i].x, (*vertices)[i].y, (*vertices)[i].z);
            if (normals && i < normals->size()) vertexData[i].normal = glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
            else vertexData[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
            if (uvs && i < uvs->size()) vertexData[i].texCoords = glm::vec2((*uvs)[i].u, (*uvs)[i].v);
            else vertexData[i].texCoords = glm::vec2(0.0f, 0.0f);
            if (colors && i < colors->size()) {
                const auto& c = (*colors)[i];
                vertexData[i].color = glm::vec4(c.r, c.g, c.b, c.a);
            }
            else {
                vertexData[i].color = glm::vec4(1.0f);
            }
            if (tangents && i < tangents->size()) vertexData[i].tangent = glm::vec3((*tangents)[i].x, (*tangents)[i].y, (*tangents)[i].z);
            if (bitangents && i < bitangents->size()) vertexData[i].bitangent = glm::vec3((*bitangents)[i].x, (*bitangents)[i].y, (*bitangents)[i].z);
        }
        auto end_stage2 = std::chrono::high_resolution_clock::now();

        MeshShape mesh;
        mesh.name = niShape->name.get();
        std::string shapeName = niShape->name.get();

        if (const auto* triShape = dynamic_cast<const nifly::BSTriShape*>(niShape)) {
            if (triShape->HasEyeData()) {
                mesh.isEye = true;
            }
        }

        // --- FINALIZED TRANSFORM LOGIC ---
        // This is the final calculated transform for this shape before converting to a GLM matrix.
        // It represents the transformation from the shape's local space to the NIF root space (Z-up).
        nifly::MatTransform finalShapeToNifRoot_transform_zUp_nifly; // Default to identity

        if (isHybridModel && niShape->IsSkinned()) {
            if (debugMode) std::cout << "    [Debug] Using identity transform for hybrid skinned part.\n";
        }
        else {
            finalShapeToNifRoot_transform_zUp_nifly = GetAVObjectTransformToGlobal(nif, niShape, false);
            bool isPrimaryHead = (shapeName == primaryHeadShapeName);

            // --- HEURISTIC FOR MISPLACED ACCESSORIES ---
            // Some FaceGen NIFs contain accessory parts (hair, brows) that have a local rotation but no
            // translation, causing them to render at the origin. This logic detects any non-primary-head part
            // whose final transform has a near-zero translation component.
            //
            // For these misplaced accessories, their transform is replaced entirely with the transform of the
            // primary head mesh. This ensures correct positioning and is a robust fix for parts authored
            // with either a pure-identity or a rotation-only transform.
            const float ZERO_TRANSLATION_THRESHOLD = 0.1f;
            if (!isPrimaryHead && finalShapeToNifRoot_transform_zUp_nifly.translation.length() < ZERO_TRANSLATION_THRESHOLD) {
                if (debugMode) std::cout << "    [Debug] Shape '" << shapeName << "' has a near-zero translation and is not the primary head. Applying accessory offset.\n";
                finalShapeToNifRoot_transform_zUp_nifly = accessoryToNifRoot_offset_zUp_nifly;
            }
            // This handles a rare case where the primary head itself might be at the origin, falling back to the skeleton's transform.
            else if (isPrimaryHead && finalShapeToNifRoot_transform_zUp_nifly.translation.length() < ZERO_TRANSLATION_THRESHOLD) {
                if (debugMode) std::cout << "    [Debug] Primary head part '" << shapeName << "' has a near-zero translation. Applying skeleton root as fallback.\n";
                finalShapeToNifRoot_transform_zUp_nifly = skeletonRootToNifRoot_transform_zUp_nifly;
            }
        }
        // Convert the row-major nifly matrix to a column-major GLM matrix using transpose.
        mesh.shapeLocalToNifRoot_transform_zUp = glm::transpose(glm::make_mat4(&finalShapeToNifRoot_transform_zUp_nifly.ToMatrix()[0]));

        if (debugMode) {
            std::cout << "    [Matrix Calc] Shape Local -> NIF Root Transform for '" << mesh.name << "' (GLM Z-up, Col-major):\n" << glm::to_string(mesh.shapeLocalToNifRoot_transform_zUp) << std::endl;
        }

        // --- Stage 3: GPU Skinning Data Extraction ---
        auto start_stage3 = std::chrono::high_resolution_clock::now();
        if (niShape->IsSkinned()) {
            auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef());
            auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef);
            auto* skinPartition = nif.GetHeader().GetBlock<nifly::NiSkinPartition>(skinInst->skinPartitionRef);

            if (skinInst && skinData && skinPartition) {
                if (debugMode) std::cout << "    [Debug] Extracting skinning data for GPU...\n";

                mesh.skinToBonePose_transforms_zUp.resize(skinData->bones.size());
                auto boneRefIt = skinInst->boneRefs.begin();
                for (size_t i = 0; i < skinData->bones.size() && boneRefIt != skinInst->boneRefs.end(); ++i, ++boneRefIt) {
                    auto* boneNode = nif.GetHeader().GetBlock<nifly::NiNode>(*boneRefIt);
                    if (!boneNode) continue;

                    std::string boneName = boneNode->name.get();

                    // This matrix will hold the bone's animated pose transform.
                    // If a skeleton is provided, it uses its Y-up world space transforms. Otherwise,
                    // it falls back to the bone's transform within the NIF's Z-up root space.
                    glm::mat4 boneToWorld_transform = glm::mat4(1.0f);

                    if (skeleton && skeleton->hasBone(boneName)) {
                        boneToWorld_transform = skeleton->getBoneTransform(boneName);
                    }
                    else {
                        // This is the bone's transform from its local space to the NIF root space (Z-up, row-major).
                        auto boneToNifRoot_transform_zUp_nifly = GetAVObjectTransformToGlobal(nif, boneNode, false);
                        // Convert to GLM matrix (Z-up, column-major).
                        boneToWorld_transform = glm::transpose(glm::make_mat4(&boneToNifRoot_transform_zUp_nifly.ToMatrix()[0]));
                    }

                    if (debugMode) {
                        std::cout << "        [Skinning Matrix] Bone '" << boneName << "' To World Transform (GLM Z-up, Col-major):\n" << glm::to_string(boneToWorld_transform) << std::endl;
                    }

                    // This is the inverse bind pose matrix from the nifly data (Z-up, row-major).
                    // It transforms a vertex from the bone's space back to the mesh's original model space.
                    const auto& skinToBone_transform_zUp_nifly = skinData->bones[i].boneTransform;
                    // Convert to GLM matrix (Z-up, column-major).
                    glm::mat4 skinToBone_transform_zUp_glm = glm::transpose(glm::make_mat4(&skinToBone_transform_zUp_nifly.ToMatrix()[0]));

                    if (debugMode) {
                        std::cout << "        [Skinning Matrix] Inverse Bind Pose for Bone #" << i << " (GLM Z-up, Col-major):\n" << glm::to_string(skinToBone_transform_zUp_glm) << std::endl;
                    }

                    // The final matrix for the shader is: Bone's World Transform * Inverse Bind Pose Transform.
                    mesh.skinToBonePose_transforms_zUp[i] = boneToWorld_transform * skinToBone_transform_zUp_glm;

                    if (debugMode) {
                        std::cout << "        [Skinning Matrix] Final Shader Matrix for Bone #" << i << " (GLM Z-up, Col-major):\n" << glm::to_string(mesh.skinToBonePose_transforms_zUp[i]) << std::endl;
                    }
                }

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
        auto end_stage3 = std::chrono::high_resolution_clock::now();

        // Get the shader properties early to determine if the mesh is skinned.
        // This is critical for the bounds calculation in Stage 4.
        // --- Shader Property and Flag Parsing ---
        // Get all shader-related properties at once to avoid redundant checks.
        const nifly::NiShader* shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
            if (const auto* bslsp = dynamic_cast<const nifly::BSLightingShaderProperty*>(shader)) {
                // Parse all flags from the shader property into a struct just once.
                ShaderFlagSet flags = ParseShaderFlags(bslsp->shaderFlags1, bslsp->shaderFlags2);

                // Check for skinned flag *before* bounds calculation.
                if (flags.SLSF1_Skinned) {
                    mesh.isSkinned = true;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Skinned.\n";
                    }
                }

                // --- Shader Property and Flag Parsing ---
                // Get all shader-related properties at once to avoid redundant checks.
                // This is done before bounds calculation because the 'isSkinned' flag is needed.
                mesh.materialAlpha = bslsp->alpha;
                if (debugMode) {
                    std::cout << "    [Material] Shape '" << mesh.name << "' has alpha: " << mesh.materialAlpha << "\n";
                    std::cout << "    [Flag Parse] Parsed shader flags for shape '" << mesh.name << "':\n";
                    std::cout << "    [Flag Parse] shaderFlags1 (raw: " << bslsp->shaderFlags1 << "): " << GetFlagsString(flags, 1) << "\n";
                    std::cout << "    [Flag Parse] shaderFlags2 (raw: " << bslsp->shaderFlags2 << "): " << GetFlagsString(flags, 2) << "\n";
                }

                if (flags.SLSF1_Specular) {
                    mesh.hasSpecularFlag = true;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Specular.\n";
                    }
                }

                if (flags.SLSF1_Environment_Mapping) {
                    mesh.hasEnvMapFlag = true;
                    mesh.envMapScale = bslsp->environmentMapScale;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Environment_Mapping.\n";
                    }
                }

                if (flags.SLSF1_Eye_Environment_Mapping) {
                    mesh.hasEyeEnvMapFlag = true;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Eye_Environment_Mapping.\n";
                    }
                }

                mesh.doubleSided = flags.SLSF2_Double_Sided;
                mesh.zBufferWrite = flags.SLSF2_ZBuffer_Write;

                if (flags.SLSF1_Receive_Shadows) {
                    mesh.receiveShadows = true;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Receive_Shadows ENABLED.\n";
                    }
                }

                if (flags.SLSF1_Cast_Shadows) {
                    mesh.castShadows = true;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Cast_Shadows ENABLED.\n";
                    }
                }

                if (flags.SLSF1_Own_Emit) {
                    mesh.hasOwnEmitFlag = true;
                    const auto& color = bslsp->emissiveColor;
                    mesh.emissiveColor = glm::vec3(color.x, color.y, color.z);
                    mesh.emissiveMultiple = bslsp->emissiveMultiple;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Own_Emit ENABLED.\n";
                    }
                }

                const auto shaderType = bslsp->GetShaderType();
                if (shaderType == nifly::BSLSP_HAIRTINT) {
                    mesh.hasTintColor = true;
                    const auto& color = bslsp->hairTintColor;
                    mesh.tintColor = glm::vec3(color.x, color.y, color.z);
                    if (debugMode) {
                        std::cout << "    [Shader Type] Shape '" << mesh.name << "' has Hair Tint enabled.\n";
                    }
                }
                else if (shaderType == nifly::BSLSP_SKINTINT || shaderType == nifly::BSLSP_FACE) {
                    mesh.hasTintColor = true;
                    const auto& color = bslsp->skinTintColor;
                    mesh.tintColor = glm::vec3(color.x, color.y, color.z);
                    if (debugMode) {
                        std::cout << "    [Shader Type] Shape '" << mesh.name << "' has Skin/Face Tint enabled.\n";
                    }
                }
            }
        }

        // --- Stage 4: Pose-Aware Bounds Calculation (CORRECTED) ---
        auto start_stage4 = std::chrono::high_resolution_clock::now();

        // This is the base transformation for the current, un-skinned shape.
        // It transforms from the shape's local model space to the NIF root space (Z-up).
        glm::mat4 shapeLocalToNifRoot_transform_zUp_glm = mesh.shapeLocalToNifRoot_transform_zUp;

        glm::vec3 shapeMinBounds_nifRootSpace_zUp(std::numeric_limits<float>::max());
        glm::vec3 shapeMaxBounds_nifRootSpace_zUp(std::numeric_limits<float>::lowest());

        std::vector<glm::vec3> posedVertices_nifRootSpace_zUp;
        posedVertices_nifRootSpace_zUp.reserve(vertexData.size());

        if (mesh.isSkinned) { // <-- This check will now work correctly!
            if (debugMode) std::cout << "    [Debug] Performing precise, pose-aware bounds calculation.\n";
            for (const auto& vert : vertexData) {
                // Vertex position in the mesh's local bind-pose model space (Z-up).
                glm::vec4 vertexPos_modelSpace_zUp(vert.pos, 1.0f);
                // This will hold the final posed vertex position.
                glm::vec4 posedVertexPos_modelSpace_zUp(0.0f);

                float totalWeight = vert.weights.x + vert.weights.y + vert.weights.z + vert.weights.w;
                if (totalWeight > 0.0f) {
                    // This matrix is the weighted average of the bone transforms for this vertex.
                    // It transforms the vertex from bind-pose model space to the final animated pose space (Z-up).
                    glm::mat4 skinBindToPose_transform_zUp = (vert.weights.x * mesh.skinToBonePose_transforms_zUp[vert.boneIDs.x] +
                        vert.weights.y * mesh.skinToBonePose_transforms_zUp[vert.boneIDs.y] +
                        vert.weights.z * mesh.skinToBonePose_transforms_zUp[vert.boneIDs.z] +
                        vert.weights.w * mesh.skinToBonePose_transforms_zUp[vert.boneIDs.w]) / totalWeight;
                    posedVertexPos_modelSpace_zUp = skinBindToPose_transform_zUp * vertexPos_modelSpace_zUp;
                }
                else {
                    posedVertexPos_modelSpace_zUp = vertexPos_modelSpace_zUp;
                }
                // Transform the final posed vertex into the NIF's root space for bounds calculation.
                posedVertices_nifRootSpace_zUp.push_back(glm::vec3(shapeLocalToNifRoot_transform_zUp_glm * posedVertexPos_modelSpace_zUp));
            }
        }
        else {
            if (debugMode) std::cout << "    [Debug] Performing bounds calculation for unskinned mesh.\n";
            for (const auto& vert : vertexData) {
                // For unskinned meshes, just transform the vertex into the NIF's root space.
                posedVertices_nifRootSpace_zUp.push_back(glm::vec3(shapeLocalToNifRoot_transform_zUp_glm * glm::vec4(vert.pos, 1.0f)));
            }
        }

        bool isAccessoryPart = false;
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(niShape->SkinInstanceRef())) {
            for (const auto& partition : skinInst->partitions) {
                if (partition.partID == 131 || partition.partID == 130) { // SBP_131_HAIR or SBP_31_HAIR
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

        for (const auto& pos : posedVertices_nifRootSpace_zUp) {
            minBounds_nifRootSpace_zUp = glm::min(minBounds_nifRootSpace_zUp, pos);
            maxBounds_nifRootSpace_zUp = glm::max(maxBounds_nifRootSpace_zUp, pos);
            shapeMinBounds_nifRootSpace_zUp = glm::min(shapeMinBounds_nifRootSpace_zUp, pos);
            shapeMaxBounds_nifRootSpace_zUp = glm::max(shapeMaxBounds_nifRootSpace_zUp, pos);
            if (!isAccessoryPart) {
                headMinBounds_nifRootSpace_zUp = glm::min(headMinBounds_nifRootSpace_zUp, pos);
                headMaxBounds_nifRootSpace_zUp = glm::max(headMaxBounds_nifRootSpace_zUp, pos);
            }
        }

        // If this shape was identified as the primary head, store its final posed bounds.
        if (shapeName == primaryHeadShapeName) {
            headShapeMinBounds_nifRootSpace_zUp = shapeMinBounds_nifRootSpace_zUp;
            headShapeMaxBounds_nifRootSpace_zUp = shapeMaxBounds_nifRootSpace_zUp;
            bHasHeadShapeBounds = true;
            if (debugMode) {
                std::cout << "    [Head Bounds] Stored final posed bounds for primary head shape '" << shapeName << "'.\n";
            }
        }

        if (debugMode) {
            std::cout << "    [Shape Bounds] '" << shapeName << "' Min: (" << shapeMinBounds_nifRootSpace_zUp.x << ", " << shapeMinBounds_nifRootSpace_zUp.y << ", " << shapeMinBounds_nifRootSpace_zUp.z << ")\n";
            std::cout << "    [Shape Bounds] '" << shapeName << "' Max: (" << shapeMaxBounds_nifRootSpace_zUp.x << ", " << shapeMaxBounds_nifRootSpace_zUp.y << ", " << shapeMaxBounds_nifRootSpace_zUp.z << ")\n";
        }

        mesh.boundsCenter_nifRootSpace_zUp = (shapeMinBounds_nifRootSpace_zUp + shapeMaxBounds_nifRootSpace_zUp) * 0.5f;

        if (mesh.isEye) {
            glm::vec3 sum(0.0f);
            if (!posedVertices_nifRootSpace_zUp.empty()) {
                for (const auto& pos : posedVertices_nifRootSpace_zUp) {
                    sum += pos;
                }
                eyeCenter_nifRootSpace_zUp = sum / static_cast<float>(posedVertices_nifRootSpace_zUp.size());
            }
            bHasEyeCenter = true;
        }
        auto end_stage4 = std::chrono::high_resolution_clock::now();

        // --- Stage 5: Texture & Material Loading ---
        auto start_stage5 = std::chrono::high_resolution_clock::now();
        if (shader && shader->HasTextureSet()) {
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                for (size_t i = 0; i < textureSet->textures.size(); ++i) {
                    std::string texPath = textureSet->textures[i].get();
                    if (texPath.empty()) continue;

                    if (debugMode) {
                        std::string slotName = "Unknown";
                        switch (i) {
                        case 0: slotName = "Diffuse"; break;
                        case 1: slotName = "Normal"; break;
                        case 2: slotName = "Skin/Subsurface"; break;
                        case 3: slotName = "Detail"; break;
                        case 4: slotName = "Environment Map"; break;
                        case 5: slotName = "Environment Mask"; break;
                        case 6: slotName = "Face Tint Mask"; break;
                        case 7: slotName = "Specular"; break;
                        }
                        std::cout << "    [Texture Load] Shape '" << mesh.name << "' | Slot " << i << " | " << slotName << ": \"" << texPath << "\"\n";
                    }

                    // --- CORRECTED LOGIC ---
                    // Call the function once and store the full TextureInfo result.
                    TextureInfo texInfo = textureManager.loadTexture(texPath);

                    // Now assign the members of the struct to the mesh.
                    switch (i) {
                    case 0: mesh.diffuseTextureID = texInfo.id; break;
                    case 1: mesh.normalTextureID = texInfo.id; break;
                    case 2: mesh.skinTextureID = texInfo.id; break;
                    case 3: mesh.detailTextureID = texInfo.id; break;
                    case 4: // Environment Map - store both ID and target
                        mesh.environmentMapID = texInfo.id;
                        mesh.environmentMapTarget = texInfo.target;
                        break;
                    case 5: mesh.environmentMaskID = texInfo.id; break;   // Masks are always 2D
                    case 6: mesh.faceTintColorMaskID = texInfo.id; break;
                    case 7: mesh.specularTextureID = texInfo.id; break;
                    default: break;
                    }
                }
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
        auto end_stage5 = std::chrono::high_resolution_clock::now();

        // --- Stage 6: GPU Buffer Upload ---
        auto start_stage6 = std::chrono::high_resolution_clock::now();
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
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, boneIDs));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, weights));

        // Add Tangent and Bitangent attributes
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));

        glBindVertexArray(0);
        auto end_stage6 = std::chrono::high_resolution_clock::now();

        // Sort into render passes
        if (mesh.hasAlphaProperty) {
            if (mesh.alphaBlend) {
                if (debugMode) {
                    std::cout << "    [Render Pass] Shape '" << mesh.name << "' assigned to TRANSPARENT pass due to alpha blending.\n";
                }
                transparentShapes.push_back(mesh);
            }
            else if (mesh.alphaTest) {
                if (debugMode) {
                    std::cout << "    [Render Pass] Shape '" << mesh.name << "' assigned to ALPHA-TESTED pass due to alpha property.\n";
                }
                alphaTestShapes.push_back(mesh);
            }
            // If an alpha property exists but blend/test flags aren't explicitly set,
            // default to alpha-testing. This is crucial for hair and beards.
            else {
                if (debugMode) {
                    std::cout << "    [Render Pass] Shape '" << mesh.name << "' assigned to ALPHA-TESTED pass as a default for having an alpha property.\n";
                }
                // Force alphaTest to true so the renderer handles its threshold and double-sided properties correctly.
                mesh.alphaTest = true;
                alphaTestShapes.push_back(mesh);
            }
        }
        else {
            if (debugMode) {
                std::cout << "    [Render Pass] Shape '" << mesh.name << "' assigned to OPAQUE pass (no alpha property).\n";
            }
            opaqueShapes.push_back(mesh);
        }

        auto end_preprocess = std::chrono::high_resolution_clock::now();

        // --- LOGGING ---
        if (debugMode) {
            auto dur_s1 = std::chrono::duration_cast<std::chrono::milliseconds>(end_stage1 - start_stage1);
            auto dur_s2 = std::chrono::duration_cast<std::chrono::milliseconds>(end_stage2 - start_stage2);
            auto dur_s3 = std::chrono::duration_cast<std::chrono::milliseconds>(end_stage3 - start_stage3);
            auto dur_s4 = std::chrono::duration_cast<std::chrono::milliseconds>(end_stage4 - start_stage4);
            auto dur_s5 = std::chrono::duration_cast<std::chrono::milliseconds>(end_stage5 - start_stage5);
            auto dur_s6 = std::chrono::duration_cast<std::chrono::milliseconds>(end_stage6 - start_stage6);
            auto duration_preprocess = std::chrono::duration_cast<std::chrono::milliseconds>(end_preprocess - start_preprocess);

            std::cout << "    [Profile] Nifly Vertex/Property Parsing: " << dur_s1.count() << " ms\n";
            std::cout << "    [Profile] Data Copy to Buffers: " << dur_s2.count() << " ms\n";
            std::cout << "    [Profile] Skinning Data Extraction: " << dur_s3.count() << " ms\n";
            std::cout << "    [Profile] Bounds Calculation: " << dur_s4.count() << " ms\n";
            std::cout << "    [Profile] Texture/Material Loading: " << dur_s5.count() << " ms\n";
            std::cout << "    [Profile] GPU Buffer Upload: " << dur_s6.count() << " ms\n";
            std::cout << "    [Profile] Total Shape Processing Time: " << duration_preprocess.count() << " ms\n";
        }
    }

    if (debugMode) {
        std::cout << "\n--- Load Complete ---\n";
        std::cout << "[Bounds] Final Min Bounds: (" << minBounds_nifRootSpace_zUp.x << ", " << minBounds_nifRootSpace_zUp.y << ", " << minBounds_nifRootSpace_zUp.z << ")\n";
        std::cout << "[Bounds] Final Max Bounds: (" << maxBounds_nifRootSpace_zUp.x << ", " << maxBounds_nifRootSpace_zUp.y << ", " << maxBounds_nifRootSpace_zUp.z << ")\n";
        std::cout << "Model Center: (" << getCenter_nifRootSpace_zUp().x << ", " << getCenter_nifRootSpace_zUp().y << ", " << getCenter_nifRootSpace_zUp().z << ")\n";
        std::cout << "Model Bounds Size: (" << getBoundsSize_nifRootSpace_zUp().x << ", " << getBoundsSize_nifRootSpace_zUp().y << ", " << getBoundsSize_nifRootSpace_zUp().z << ")\n";
        std::cout << "---------------------\n\n";
    }

    return true;
}

// Keep your old load function to avoid breaking things, and have it call the new one.
// This is optional but good practice.
bool NifModel::load(const std::string& nifPath, TextureManager& textureManager, const Skeleton* skeleton) {
    std::ifstream file(nifPath, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Failed to open NIF file from disk: " << nifPath << std::endl;
        return false;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return load(data, nifPath, textureManager, skeleton);
}


/**
 * @brief Renders the entire NifModel, handling opaque, alpha-tested, and transparent objects in separate passes.
 * @param shader The main shader program to use for rendering.
 * @param cameraPos The position of the camera in world space, used for sorting transparent objects.
 * @param nifRootToWorld_conversionMatrix_zUpToYUp A transformation matrix that converts from the NIF's root Z-up coordinate space to the renderer's world Y-up coordinate space.
 */
void NifModel::draw(Shader& shader, const glm::vec3& cameraPos, const glm::mat4& nifRootToWorld_conversionMatrix_zUpToYUp) {
    // Log the start of the draw call, but only for the first frame.
    renderFirstFrameLog("--- START NifModel::draw() ---");

    // Activate the main rendering shader.
    shader.use();

    // --- Set Global Shader Uniforms (Samplers) ---
    // These uniforms tell the shader which texture unit to use for each type of texture map.
    // They are set once per draw call as they are the same for all shapes.
    shader.setInt("texture_diffuse1", 0);    // Diffuse/Albedo map on TU 0
    shader.setInt("texture_normal", 1);      // Normal map on TU 1
    shader.setInt("texture_skin", 2);        // Subsurface/Skin map on TU 2
    shader.setInt("texture_detail", 3);      // Detail map on TU 3
    shader.setInt("texture_specular", 4);    // Specular map on TU 4
    shader.setInt("texture_face_tint", 5);   // Face tint mask on TU 5
    shader.setInt("texture_envmap_2d", 6);   // 2D Environment map on TU 6
    shader.setInt("texture_envmap_cube", 6); // Cubemap Environment map on TU 6 (same unit)
    shader.setInt("texture_envmask", 7);     // Environment mask on TU 7

    // Set eye-specific shader properties, used for fresnel and specular effects on eye meshes.
    shader.setFloat("eye_fresnel_strength", 0.3f);
    shader.setFloat("eye_spec_power", 80.0f);

    // Get the uniform location for the bone matrices array once, before the render loop, for efficiency.
    GLint boneMatricesLocation = glGetUniformLocation(shader.ID, "uBoneMatrices");
    if (m_logRenderPassesOnce) checkGlErrors("After getting bone uniform location");

    // A helper lambda to render a single shape. This centralizes the logic for setting
    // per-shape uniforms, binding textures, and issuing the draw command.
    auto render_shape = [&](const MeshShape& shape) {
        // --- Visibility Check ---
        if (!shape.visible) {
            renderFirstFrameLog("Shape '" + shape.name + "' is not visible, skipping.");
            return;
        }
        else {
            renderFirstFrameLog("Processing visible shape '" + shape.name + "'.");
        }
        if (m_logRenderPassesOnce) checkGlErrors(("Start of render_shape for '" + shape.name + "'").c_str());

        // --- Set Per-Shape Uniforms ---

        // Calculate and set the final model matrix. This transforms the shape's vertices from its local Z-up space
        // all the way to the renderer's world Y-up space.
        glm::mat4 modelMatrix = nifRootToWorld_conversionMatrix_zUpToYUp * shape.shapeLocalToNifRoot_transform_zUp;
        shader.setMat4("u_model_localToWorld", modelMatrix);

        // Determine if the final transform matrix for geometry has a negative scale (is mirrored).
       // The determinant of the rotation/scale part will be negative if it's a reflection.
        bool isMirrored = glm::determinant(glm::mat3(modelMatrix)) < 0;

        // We only flip UVs if the geometry is mirrored.
        shader.setBool("u_flipUvs", isMirrored);
        if (m_logRenderPassesOnce) {
            renderFirstFrameLog("  -> Is Mirrored: " + std::string(isMirrored ? "true" : "false") + ", setting u_flipUvs uniform.");
        }

        renderFirstFrameLog("  -> Final Model Matrix (Local Z-up -> World Y-up):\n" + glm::to_string(modelMatrix));

        // Set boolean flags that control shader logic.
        shader.setBool("is_eye", shape.isEye);
        shader.setBool("is_model_space", shape.isModelSpace); // For model-space normals
        shader.setBool("has_tint_color", shape.hasTintColor);

        if (shape.hasTintColor) {
            renderFirstFrameLog("  -> Has tint color, setting uniform.");
            shader.setVec3("tint_color", shape.tintColor);
        }
        else {
            renderFirstFrameLog("  -> Does not have tint color.");
        }

        shader.setBool("has_emissive", shape.hasOwnEmitFlag);
        if (shape.hasOwnEmitFlag) {
            renderFirstFrameLog("  -> Has emissive properties, setting uniforms.");
            shader.setVec3("emissiveColor", shape.emissiveColor);
            shader.setFloat("emissiveMultiple", shape.emissiveMultiple);
        }
        else {
            renderFirstFrameLog("  -> Does not have emissive properties.");
        }

        // --- Skinning ---
        // Tell the vertex shader whether this mesh is skinned.
        shader.setBool("uIsSkinned", shape.isSkinned);
        if (shape.isSkinned && !shape.skinToBonePose_transforms_zUp.empty()) {
            renderFirstFrameLog("  -> Is skinned, uploading bone matrices.");
            GLsizei boneCount = shape.skinToBonePose_transforms_zUp.size();

            // Sanity check: ensure the bone count does not exceed the shader's array size to prevent a crash.
            if (boneCount > MAX_BONES) {
                renderFirstFrameLog("    -> WARNING: Bone count (" + std::to_string(boneCount) + ") exceeds MAX_BONES (" + std::to_string(MAX_BONES) + "). Clamping.");
                std::cerr << "!!! WARNING: Shape '" << shape.name
                    << "' has " << boneCount
                    << " bones, which exceeds the shader limit of " << MAX_BONES
                    << ". Clamping bone count." << std::endl;
                boneCount = MAX_BONES;
            }
            else {
                renderFirstFrameLog("    -> Bone count (" + std::to_string(boneCount) + ") is within shader limit.");
            }

            // Upload the array of final bone transformation matrices to the GPU.
            glUniformMatrix4fv(boneMatricesLocation, boneCount, GL_FALSE, glm::value_ptr(shape.skinToBonePose_transforms_zUp[0]));
        }
        else {
            renderFirstFrameLog("  -> Is not skinned.");
        }
        if (m_logRenderPassesOnce) checkGlErrors(("After setting uniforms for '" + shape.name + "'").c_str());


        // --- Texture Binding ---
        // Bind the diffuse texture to texture unit 0.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);

        // For each subsequent texture, set a boolean uniform to tell the fragment shader
        // whether a valid texture exists. If it does, activate the corresponding texture
        // unit and bind the texture.
        shader.setBool("has_normal_map", shape.normalTextureID != 0);
        if (shape.normalTextureID != 0) {
            renderFirstFrameLog("  -> Has normal map, binding texture.");
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shape.normalTextureID);
        }
        else {
            renderFirstFrameLog("  -> Does not have normal map.");
        }

        shader.setBool("has_skin_map", shape.skinTextureID != 0);
        if (shape.skinTextureID != 0) {
            renderFirstFrameLog("  -> Has skin map, binding texture.");
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, shape.skinTextureID);
        }
        else {
            renderFirstFrameLog("  -> Does not have skin map.");
        }

        shader.setBool("has_detail_map", shape.detailTextureID != 0);
        if (shape.detailTextureID != 0) {
            renderFirstFrameLog("  -> Has detail map, binding texture.");
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, shape.detailTextureID);
        }
        else {
            renderFirstFrameLog("  -> Does not have detail map.");
        }

        shader.setBool("has_specular", shape.hasSpecularFlag);
        shader.setBool("has_specular_map", shape.specularTextureID != 0);
        if (shape.specularTextureID != 0) {
            renderFirstFrameLog("  -> Has specular map, binding texture.");
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, shape.specularTextureID);
        }
        else {
            renderFirstFrameLog("  -> Does not have specular map.");
        }

        shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
        if (shape.faceTintColorMaskID != 0) {
            renderFirstFrameLog("  -> Has face tint map, binding texture.");
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID);
        }
        else {
            renderFirstFrameLog("  -> Does not have face tint map.");
        }

        // --- Environment Mapping ---
        // An "effective" environment map exists only if the material flag is set AND a texture is assigned.
        bool useEffectiveEnvMap = shape.hasEnvMapFlag && shape.environmentMapID != 0;
        shader.setBool("has_environment_map", useEffectiveEnvMap);
        shader.setBool("has_eye_environment_map", shape.hasEyeEnvMapFlag);

        if (useEffectiveEnvMap || shape.hasEyeEnvMapFlag) {
            renderFirstFrameLog("  -> Has effective env map or eye env map flag, setting env map uniforms.");
            shader.setBool("is_envmap_cube", shape.environmentMapTarget == GL_TEXTURE_CUBE_MAP);
            shader.setFloat("envMapScale", shape.envMapScale);
        }
        else {
            renderFirstFrameLog("  -> Does not have an active environment map.");
        }

        if (shape.environmentMapID != 0) {
            renderFirstFrameLog("  -> Has environment map texture, binding it.");
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(shape.environmentMapTarget, shape.environmentMapID);
        }
        else {
            renderFirstFrameLog("  -> Does not have an environment map texture ID.");
        }

        if (shape.environmentMaskID != 0) {
            renderFirstFrameLog("  -> Has environment mask, binding it.");
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, shape.environmentMaskID);
        }
        else {
            renderFirstFrameLog("  -> Does not have an environment mask ID.");
        }
        if (m_logRenderPassesOnce) checkGlErrors(("After binding textures for '" + shape.name + "'").c_str());

        // --- Draw Call ---
        // With all uniforms and textures set, issue the command to draw the shape's geometry.
        shape.draw();
        if (m_logRenderPassesOnce) checkGlErrors(("IMMEDIATELY AFTER shape.draw() for '" + shape.name + "'").c_str());
        };

    // =========================================================================================
    // --- PASS 1: OPAQUE OBJECTS ---
    // Render all fully opaque objects first. This establishes the scene's depth, allowing
    // subsequent passes to correctly depth-test against these objects.
    // =========================================================================================
    renderFirstFrameLog("--- Pass 1: Opaque Objects (" + std::to_string(opaqueShapes.size()) + " shapes) ---");
    glEnable(GL_DEPTH_TEST); // Enable depth testing.
    glDepthMask(GL_TRUE);    // Allow writing to the depth buffer.
    glDisable(GL_BLEND);     // Disable color blending.
    glEnable(GL_CULL_FACE);  // Enable back-face culling for performance.
    glCullFace(GL_BACK);
    shader.setBool("use_alpha_test", false); // Alpha testing is not needed for this pass.

    if (m_logRenderPassesOnce) checkGlErrors("Before opaque loop");
    for (const auto& shape : opaqueShapes) {
        render_shape(shape);
    }
    if (m_logRenderPassesOnce) checkGlErrors("After opaque loop");

    // =========================================================================================
    // --- PASS 2: ALPHA-TEST (CUTOUT) OBJECTS ---
    // Render objects with cutout transparency (e.g., hair, grates). The fragment shader
    // will 'discard' pixels with an alpha value below a threshold, creating a hard edge.
    // These objects still write to the depth buffer.
    // =========================================================================================
    renderFirstFrameLog("--- Pass 2: Alpha-Test (Cutout) Objects (" + std::to_string(alphaTestShapes.size()) + " shapes) ---");
    shader.setBool("use_alpha_test", true); // Enable alpha testing in the shader.
    for (const auto& shape : alphaTestShapes) {
        shader.setFloat("alpha_threshold", shape.alphaThreshold);

        // For thin meshes like hair, disable back-face culling to ensure they are visible from both sides.
        if (shape.doubleSided) {
            renderFirstFrameLog("Shape '" + shape.name + "' is double-sided, disabling cull face.");
            glDisable(GL_CULL_FACE);
            render_shape(shape);
            glEnable(GL_CULL_FACE); // Re-enable culling immediately after.
        }
        else {
            renderFirstFrameLog("Shape '" + shape.name + "' is single-sided.");
            render_shape(shape);
        }
    }
    shader.setBool("use_alpha_test", false); // Disable alpha testing for the next pass.

    // =========================================================================================
    // --- PASS 3: TRANSPARENT (ALPHA-BLEND) OBJECTS ---
    // Render truly transparent objects last. They must be sorted from back-to-front to
    // ensure colors blend correctly. They test against the depth buffer but DO NOT write
    // to it, preventing objects behind them from being incorrectly culled.
    // =========================================================================================
    renderFirstFrameLog("--- Pass 3: Transparent (Alpha-Blend) Objects (" + std::to_string(transparentShapes.size()) + " shapes) ---");
    if (!transparentShapes.empty()) {
        renderFirstFrameLog("Transparent shapes list is not empty. Sorting and rendering.");
        // Sort shapes based on their distance from the camera (farthest first).
        std::sort(transparentShapes.begin(), transparentShapes.end(),
            [&cameraPos](const MeshShape& a, const MeshShape& b) {
                return glm::distance2(a.boundsCenter_nifRootSpace_zUp, cameraPos) > glm::distance2(b.boundsCenter_nifRootSpace_zUp, cameraPos);
            });

        glEnable(GL_BLEND);      // Enable color blending.
        glDepthMask(GL_FALSE);   // CRITICAL: Disable depth writes to prevent transparency artifacts.

        if (m_logRenderPassesOnce) checkGlErrors("Before transparent loop");
        for (const auto& shape : transparentShapes) {
            // Set the specific blend function (e.g., SRC_ALPHA, ONE_MINUS_SRC_ALPHA) for this shape.
            glBlendFunc(shape.srcBlend, shape.dstBlend);
            if (shape.doubleSided) {
                renderFirstFrameLog("Shape '" + shape.name + "' is double-sided, disabling cull face.");
                glDisable(GL_CULL_FACE);
            }
            else {
                renderFirstFrameLog("Shape '" + shape.name + "' is single-sided, enabling cull face.");
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            render_shape(shape);
        }
        if (m_logRenderPassesOnce) checkGlErrors("After transparent loop");
    }
    else {
        renderFirstFrameLog("Transparent shapes list is empty, skipping pass.");
    }

    // --- Reset to default OpenGL state ---
    // Restore the default state to ensure this function doesn't interfere with other rendering code (e.g., UI).
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glActiveTexture(GL_TEXTURE0); // Reset active texture unit to the default.
    if (m_logRenderPassesOnce) checkGlErrors("End of NifModel::draw");

    // After the first frame has been fully drawn and logged, set the flag to false.
    m_logRenderPassesOnce = false;
    renderFirstFrameLog("--- END NifModel::draw() | Logging disabled for subsequent frames. ---");
}

void NifModel::drawDepthOnly(Shader& depthShader) {
    depthShader.use();

    GLint boneMatricesLocation = glGetUniformLocation(depthShader.ID, "uBoneMatrices");

    auto render_shape_depth = [&](const MeshShape& shape, bool isAlphaTested) {
        if (!shape.visible) return;

        // In this engine, we assume objects that cast shadows also receive them.
        // This is a common simplification.
        if (!shape.receiveShadows) return;

        // Only render shapes that are flagged to cast shadows into the depth map.
        if (!shape.castShadows) return;

        // --- NEW: Handle alpha testing for the depth pass ---
        depthShader.setBool("use_alpha_test", isAlphaTested);
        if (isAlphaTested) {
            // Bind the diffuse texture to unit 0 for the alpha test
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);
            depthShader.setInt("texture_diffuse1", 0);
            depthShader.setFloat("alpha_threshold", shape.alphaThreshold);
        }

        depthShader.setMat4("u_modelToNifRoot_transform_zUp", shape.shapeLocalToNifRoot_transform_zUp);
        depthShader.setBool("uIsSkinned", shape.isSkinned);

        if (shape.isSkinned && !shape.skinToBonePose_transforms_zUp.empty()) {
            GLsizei boneCount = static_cast<GLsizei>(shape.skinToBonePose_transforms_zUp.size());
            if (boneCount > MAX_BONES) boneCount = MAX_BONES;
            glUniformMatrix4fv(boneMatricesLocation, boneCount, GL_FALSE, glm::value_ptr(shape.skinToBonePose_transforms_zUp[0]));
        }
        shape.draw();
        };

    // Opaque shapes do not need alpha testing in the depth pass.
    for (const auto& shape : opaqueShapes) {
        render_shape_depth(shape, false);
    }

    // Alpha-tested shapes DO need alpha testing in the depth pass.
    for (const auto& shape : alphaTestShapes) {
        render_shape_depth(shape, true);
    }
    // Transparent (alpha-blended) shapes typically do not cast shadows, so they are skipped.
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

void NifModel::renderFirstFrameLog(const std::string& message) {
    if (m_logRenderPassesOnce) {
        std::cout << "[Render Logic] " << message << std::endl;
    }
}