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

// Helper function to get the full world transform of any scene graph object (Node or Shape)
nifly::MatTransform GetAVObjectTransformToGlobal(const nifly::NifFile& nifFile, nifly::NiAVObject* obj, bool debugMode = false) {
    if (!obj) {
        return nifly::MatTransform();
    }
    nifly::MatTransform xform = obj->GetTransformToParent();
    nifly::NiNode* parent = nifFile.GetParentNode(obj);
    while (parent) {
        xform = parent->GetTransformToParent().ComposeTransforms(xform);
        parent = nifFile.GetParentNode(parent);
    }
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

// --- HELPER FUNCTIONS FOR SKELETON ROOT FINDING ---

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

    // Reset bounds for the new model
    minBounds = glm::vec3(std::numeric_limits<float>::max());
    maxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    if (debugMode) {
        std::cout << "[Bounds] Initial Min Bounds: (" << minBounds.x << ", " << minBounds.y << ", " << minBounds.z << ")\n";
        std::cout << "[Bounds] Initial Max Bounds: (" << maxBounds.x << ", " << maxBounds.y << ", " << maxBounds.z << ")\n";
    }
    headMinBounds = glm::vec3(std::numeric_limits<float>::max());
    headMaxBounds = glm::vec3(std::numeric_limits<float>::lowest());

    // --- MODIFICATION: Initialize new head shape bound members ---
    headShapeMinBounds = glm::vec3(std::numeric_limits<float>::max());
    headShapeMaxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    bHasHeadShapeBounds = false;

    bHasEyeCenter = false;

    // --- HEURISTIC TO DETECT NIF TYPE (Standard vs. Hybrid) ---
    // This logic is crucial for handling different ways NIF files are authored.
    bool isHybridModel = false;
    const float PRETRANSLATED_THRESHOLD = 10.0f;
    for (auto* shape : shapeList) {
        if (!shape->IsSkinned()) continue;
        nifly::MatTransform shapeTransform = GetAVObjectTransformToGlobal(nif, shape, false);
        // A hybrid model has skinned parts with no transform, but their vertices are far from the origin.
        if (shapeTransform.IsNearlyEqualTo(nifly::MatTransform())) {
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

    nifly::MatTransform accessoryOffset;
    for (auto* shape : shapeList) {
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
            for (const auto& partition : skinInst->partitions) {
                if (partition.partID == 30 || partition.partID == 230) { // Head partitions
                    accessoryOffset = GetAVObjectTransformToGlobal(nif, shape, false);
                    goto found_head;
                }
            }
        }
    }
found_head:

    // Find the skeleton root's transform, which is needed for some hybrid NIFs.
    nifly::MatTransform skeletonRootTransform; // Default constructs to identity
    for (auto* shape : shapeList) {
        if (shape->IsSkinned()) {
            if (auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(shape->SkinInstanceRef())) {
                auto* skelRootNode = FindSkeletonRootLCA(nif, skinInst);
                if (skelRootNode) {
                    skeletonRootTransform = GetAVObjectTransformToGlobal(nif, skelRootNode);
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
        nifly::MatTransform niflyTransform; // Default to identity
        if (isHybridModel && niShape->IsSkinned()) {
            if (debugMode) std::cout << "    [Debug] Using identity transform for hybrid skinned part.\n";
        }
        else {
            niflyTransform = GetAVObjectTransformToGlobal(nif, niShape, false);
            if (niflyTransform.IsNearlyEqualTo(nifly::MatTransform())) {
                bool isLocalSpacePart = (
                    shapeName.find("Eyes") != std::string::npos ||
                    shapeName.find("Mouth") != std::string::npos ||
                    shapeName.find("Teeth") != std::string::npos ||
                    shapeName.find("Brows") != std::string::npos
                    );

                if (isLocalSpacePart) {
                    if (debugMode) std::cout << "    [Debug] Applying accessory offset for local-space part '" << shapeName << "'.\n";
                    niflyTransform = accessoryOffset;
                }
                else {
                    // This is the restored logic for pre-translated parts in non-hybrid models.
                    // Their vertices are relative to the skeleton root, so we apply its transform.
                    if (debugMode) std::cout << "    [Debug] Applying skeleton root transform for pre-translated part '" << shapeName << "'.\n";
                    niflyTransform = skeletonRootTransform;
                }
            }
        }
        mesh.transform = glm::transpose(glm::make_mat4(&niflyTransform.ToMatrix()[0]));

        // --- Stage 3: GPU Skinning Data Extraction ---
        auto start_stage3 = std::chrono::high_resolution_clock::now();
        if (niShape->IsSkinned()) {
            auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef());
            auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef);
            auto* skinPartition = nif.GetHeader().GetBlock<nifly::NiSkinPartition>(skinInst->skinPartitionRef);

            if (skinInst && skinData && skinPartition) {
                if (debugMode) std::cout << "    [Debug] Extracting skinning data for GPU...\n";

                mesh.boneMatrices.resize(skinData->bones.size());
                auto boneRefIt = skinInst->boneRefs.begin();
                for (size_t i = 0; i < skinData->bones.size() && boneRefIt != skinInst->boneRefs.end(); ++i, ++boneRefIt) {
                    auto* boneNode = nif.GetHeader().GetBlock<nifly::NiNode>(*boneRefIt);
                    if (!boneNode) continue;

                    std::string boneName = boneNode->name.get();
                    glm::mat4 boneWorld = glm::mat4(1.0f);
                    if (skeleton && skeleton->hasBone(boneName)) {
                        boneWorld = skeleton->getBoneTransform(boneName);
                    }
                    else {
                        auto boneWorld_n = GetAVObjectTransformToGlobal(nif, boneNode, false);
                        boneWorld = glm::transpose(glm::make_mat4(&boneWorld_n.ToMatrix()[0]));
                    }
                    const auto& skinToBone_n = skinData->bones[i].boneTransform;
                    glm::mat4 skinToBone = glm::transpose(glm::make_mat4(&skinToBone_n.ToMatrix()[0]));
                    mesh.boneMatrices[i] = boneWorld * skinToBone;
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

        const nifly::NiShader* shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
        }

        // --- Stage 4: Pose-Aware Bounds Calculation (CORRECTED) ---
        auto start_stage4 = std::chrono::high_resolution_clock::now();
        glm::mat4 shapeBaseTransform = mesh.transform;
        glm::vec3 shapeMinBounds(std::numeric_limits<float>::max());
        glm::vec3 shapeMaxBounds(std::numeric_limits<float>::lowest());

        std::vector<glm::vec3> posedVertices;
        posedVertices.reserve(vertexData.size());

        if (mesh.isSkinned) {
            if (debugMode) std::cout << "    [Debug] Performing precise, pose-aware bounds calculation.\n";
            for (const auto& vert : vertexData) {
                glm::vec4 originalPos(vert.pos, 1.0f);
                glm::vec4 finalPos(0.0f);

                float totalWeight = vert.weights.x + vert.weights.y + vert.weights.z + vert.weights.w;
                if (totalWeight > 0.0f) {
                    glm::mat4 skinMatrix = (vert.weights.x * mesh.boneMatrices[vert.boneIDs.x] +
                        vert.weights.y * mesh.boneMatrices[vert.boneIDs.y] +
                        vert.weights.z * mesh.boneMatrices[vert.boneIDs.z] +
                        vert.weights.w * mesh.boneMatrices[vert.boneIDs.w]) / totalWeight;
                    finalPos = skinMatrix * originalPos;
                }
                else {
                    finalPos = originalPos;
                }
                posedVertices.push_back(glm::vec3(shapeBaseTransform * finalPos));
            }
        }
        else {
            if (debugMode) std::cout << "    [Debug] Performing bounds calculation for unskinned mesh.\n";
            for (const auto& vert : vertexData) {
                posedVertices.push_back(glm::vec3(shapeBaseTransform * glm::vec4(vert.pos, 1.0f)));
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

        for (const auto& pos : posedVertices) {
            minBounds = glm::min(minBounds, pos);
            maxBounds = glm::max(maxBounds, pos);
            shapeMinBounds = glm::min(shapeMinBounds, pos);
            shapeMaxBounds = glm::max(shapeMaxBounds, pos);
            if (!isAccessoryPart) {
                headMinBounds = glm::min(headMinBounds, pos);
                headMaxBounds = glm::max(headMaxBounds, pos);
            }
        }

        // --- MODIFICATION START: Check for the head partition and store its specific bounds ---
        if (!bHasHeadShapeBounds) { // Only capture the first one found
            if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(niShape->SkinInstanceRef())) {
                for (const auto& partition : skinInst->partitions) {
                    // SBP_30_HEAD or SBP_230_HEAD (beast)
                    if (partition.partID == 30 || partition.partID == 230) {
                        headShapeMinBounds = shapeMinBounds;
                        headShapeMaxBounds = shapeMaxBounds;
                        bHasHeadShapeBounds = true;
                        if (debugMode) {
                            std::cout << "    [Head Bounds] Captured specific bounds from '" << shapeName << "' via partition ID " << partition.partID << ".\n";
                        }
                        break; // Found the head partition for this shape
                    }
                }
            }
        }

        if (debugMode) {
            std::cout << "    [Shape Bounds] '" << shapeName << "' Min: (" << shapeMinBounds.x << ", " << shapeMinBounds.y << ", " << shapeMinBounds.z << ")\n";
            std::cout << "    [Shape Bounds] '" << shapeName << "' Max: (" << shapeMaxBounds.x << ", " << shapeMaxBounds.y << ", " << shapeMaxBounds.z << ")\n";
        }

        mesh.boundsCenter = (shapeMinBounds + shapeMaxBounds) * 0.5f;

        if (mesh.isEye) {
            glm::vec3 sum(0.0f);
            if (!posedVertices.empty()) {
                for (const auto& pos : posedVertices) {
                    sum += pos;
                }
                eyeCenter = sum / static_cast<float>(posedVertices.size());
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

        if (const auto* bslsp = dynamic_cast<const nifly::BSLightingShaderProperty*>(shader)) {
            ShaderFlagSet flags = ParseShaderFlags(bslsp->shaderFlags1, bslsp->shaderFlags2);
		    std::cout << "    [Flag Parse] Parsed shader flags for shape '" << mesh.name << " (Debug Only; these are not all used for rendering)':\n";
            std::cout << "    [Flag Parse] shaderFlags1 (raw: " << bslsp->shaderFlags1 << "): " << GetFlagsString(flags, 1) << "\n";
            std::cout << "    [Flag Parse] shaderFlags2 (raw: " << bslsp->shaderFlags2 << "): " << GetFlagsString(flags, 2) << "\n";

            if (bslsp->shaderFlags1 & (1U << 0)) { // (1U << 0) is the mask for SLSF1_Specular
                mesh.hasSpecularFlag = true;
                if (debugMode) {
                    std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Specular.\n";
                }
            }

            if (bslsp->shaderFlags1 & (1U << 1)) { // Check for the SLSF1_Skinned flag (bit 1)
                mesh.isSkinned = true;
                if (debugMode) {
                    std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Skinned.\n";
                }
            }

            if (bslsp->shaderFlags1 & (1U << 2)) { // Check for SLSF1_Environment_Mapping (bit 2)
                mesh.hasEnvMapFlag = true;
                // Also grab the environment map scale from the shader property
                mesh.envMapScale = bslsp->environmentMapScale;
                    if (debugMode) {
                        std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Environment_Mapping.\n";
                    }
            }
            
            if (bslsp->shaderFlags1 & (1U << 10)) { // Check for SLSF1_Eye_Environment_Mapping (bit 10)
                mesh.hasEyeEnvMapFlag = true;
                if (debugMode) {
                    std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF1_Eye_Environment_Mapping.\n";
                }
            }

            // --- NEW FLAG CHECKS for shaderFlags2 ---
            mesh.doubleSided = (bslsp->shaderFlags2 & (1U << 4));
            mesh.zBufferWrite = (bslsp->shaderFlags2 & (1U << 0));

            // Check for SLSF2_Recieve_Shadows (bit 1). 
            // The flag being set means it DOES receive shadows.
            if (bslsp->shaderFlags2 & (1U << 1)) {
                mesh.receiveShadows = true;
                if (debugMode) {
                    std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF2_Recieve_Shadows ENABLED.\n";
                }
            }

            // Check for SLSF2_Cast_Shadows (bit 2).
            if (bslsp->shaderFlags2 & (1U << 2)) {
                mesh.castShadows = true;
                if (debugMode) {
                    std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF2_Cast_Shadows ENABLED.\n";
                }
            }

            // Check for SLSF2_Own_Emit (bit 3)
            if (bslsp->shaderFlags2 & (1U << 3)) {
                mesh.hasOwnEmitFlag = true;
                const auto& color = bslsp->emissiveColor;
                mesh.emissiveColor = glm::vec3(color.x, color.y, color.z);
                mesh.emissiveMultiple = bslsp->emissiveMultiple;
                if (debugMode) {
                    std::cout << "    [Flag Detect] Shape '" << mesh.name << "' has flag SLSF2_Own_Emit ENABLED.\n";
                }
            }

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
            if (mesh.alphaBlend) transparentShapes.push_back(mesh);
            else if (mesh.alphaTest) alphaTestShapes.push_back(mesh);
            else opaqueShapes.push_back(mesh);
        }
        else {
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
        std::cout << "[Bounds] Final Min Bounds: (" << minBounds.x << ", " << minBounds.y << ", " << minBounds.z << ")\n";
        std::cout << "[Bounds] Final Max Bounds: (" << maxBounds.x << ", " << maxBounds.y << ", " << maxBounds.z << ")\n";
        std::cout << "Model Center: (" << getCenter().x << ", " << getCenter().y << ", " << getCenter().z << ")\n";
        std::cout << "Model Bounds Size: (" << getBoundsSize().x << ", " << getBoundsSize().y << ", " << getBoundsSize().z << ")\n";
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


void NifModel::draw(Shader& shader, const glm::vec3& cameraPos) {
    /*
    std::cout << "\n--- [Debug Render] ---" << std::endl;
    std::cout << "  Opaque Shapes: " << opaqueShapes.size() << std::endl;
    std::cout << "  Alpha Test Shapes: " << alphaTestShapes.size() << std::endl;
    std::cout << "  Transparent Shapes: " << transparentShapes.size() << std::endl;
    */

    shader.use();
    shader.setInt("texture_diffuse1", 0);
    shader.setInt("texture_normal", 1);
    shader.setInt("texture_skin", 2);
    shader.setInt("texture_detail", 3);
    shader.setInt("texture_specular", 4);
    shader.setInt("texture_face_tint", 5);
    shader.setInt("texture_envmap_2d", 6);   // <-- ADD
    shader.setInt("texture_envmap_cube", 6); // <-- ADD (points to the same texture unit)
    shader.setInt("texture_envmask", 7);

    shader.setFloat("eye_fresnel_strength", 0.3f);
    shader.setFloat("eye_spec_power", 80.0f);

    // Get location for bone matrix uniform array (cache it for performance)
    GLint boneMatricesLocation = glGetUniformLocation(shader.ID, "uBoneMatrices");
    checkGlErrors("After getting bone uniform location"); // <<-- ADD

    // Helper lambda to render a single shape, setting all its unique uniforms
    auto render_shape = [&](const MeshShape& shape) {
        if (!shape.visible) return;

        checkGlErrors(("Start of render_shape for '" + shape.name + "'").c_str()); // <<-- ADD

        shader.setMat4("model", shape.transform);
        shader.setBool("is_eye", shape.isEye);
        shader.setBool("is_model_space", shape.isModelSpace); // Set model space uniform
        shader.setBool("has_tint_color", shape.hasTintColor);
        if (shape.hasTintColor) {
            shader.setVec3("tint_color", shape.tintColor);
        }
        shader.setBool("has_emissive", shape.hasOwnEmitFlag);
        if (shape.hasOwnEmitFlag) {
            shader.setVec3("emissiveColor", shape.emissiveColor);
            shader.setFloat("emissiveMultiple", shape.emissiveMultiple);
        }

        // Set skinning uniforms for the vertex shader
        shader.setBool("uIsSkinned", shape.isSkinned);
        if (shape.isSkinned && !shape.boneMatrices.empty()) {
            GLsizei boneCount = shape.boneMatrices.size();
            if (boneCount > MAX_BONES) {
                std::cerr << "!!! WARNING: Shape '" << shape.name // <-- You'll need to add 'name' to MeshShape struct
                    << "' has " << boneCount
                    << " bones, which exceeds the shader limit of " << MAX_BONES
                    << ". Clamping bone count." << std::endl;
                boneCount = MAX_BONES;
            }

            /*
            std::cout << "  [Debug Skinning] Uploading uniforms for shape '" << shape.name
                << "': Bone Count = " << boneCount
                << ", Location = " << boneMatricesLocation << std::endl;
				*/

            glUniformMatrix4fv(boneMatricesLocation, boneCount, GL_FALSE, glm::value_ptr(shape.boneMatrices[0]));
        }

        checkGlErrors(("After setting uniforms for '" + shape.name + "'").c_str()); // <<-- ADD


        // Bind all textures and set corresponding 'has_' flags for the fragment shader
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);

        shader.setBool("has_normal_map", shape.normalTextureID != 0);
        if (shape.normalTextureID != 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shape.normalTextureID); }

        shader.setBool("has_skin_map", shape.skinTextureID != 0);
        if (shape.skinTextureID != 0) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shape.skinTextureID); }

        shader.setBool("has_detail_map", shape.detailTextureID != 0);
        if (shape.detailTextureID != 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, shape.detailTextureID); }

        // Set a uniform to tell the shader if the specular flag is on.
        shader.setBool("has_specular", shape.hasSpecularFlag);

        // Separately, set a uniform to tell the shader if a texture map exists.
        shader.setBool("has_specular_map", shape.specularTextureID != 0);
        if (shape.specularTextureID != 0) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shape.specularTextureID); }

        shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
        if (shape.faceTintColorMaskID != 0) { glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID); }

        bool useEffectiveEnvMap = shape.hasEnvMapFlag && shape.environmentMapID != 0;
        shader.setBool("has_environment_map", useEffectiveEnvMap);
        shader.setBool("has_eye_environment_map", shape.hasEyeEnvMapFlag);
        if (useEffectiveEnvMap || shape.hasEyeEnvMapFlag) { 
            shader.setBool("is_envmap_cube", shape.environmentMapTarget == GL_TEXTURE_CUBE_MAP);
            shader.setFloat("envMapScale", shape.envMapScale);
        }

        if (shape.environmentMapID != 0) {
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(shape.environmentMapTarget, shape.environmentMapID);
        }
        if (shape.environmentMaskID != 0) {
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, shape.environmentMaskID);
        }
        checkGlErrors(("After binding textures for '" + shape.name + "'").c_str()); // <<-- ADD


        shape.draw();
        checkGlErrors(("IMMEDIATELY AFTER shape.draw() for '" + shape.name + "'").c_str()); // <<-- CRITICAL: ADD THIS

        };

    // --- PASS 1: OPAQUE OBJECTS ---
    // Render all fully opaque objects first. They will write to the depth buffer,
    // establishing the scene's depth for later passes.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
  
    shader.setBool("use_alpha_test", false);
    checkGlErrors("Before opaque loop"); // <<-- ADD
    for (const auto& shape : opaqueShapes) {
        render_shape(shape);
    }
    checkGlErrors("After opaque loop"); // <<-- ADD

    // --- PASS 2: ALPHA-TEST (CUTOUT) OBJECTS ---
    // Render objects with cutout transparency (like hair or grates).
    // These objects test against the depth buffer and also write to it.
    // MSAA is used to smooth the hard edges of the cutouts.
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    shader.setBool("use_alpha_test", true);
    checkGlErrors("Before alpha-test loop"); // <<-- ADD
    for (const auto& shape : alphaTestShapes) {
        shader.setFloat("alpha_threshold", shape.alphaThreshold);

        if (shape.doubleSided) {
            glCullFace(GL_FRONT); // Pass 1: Draw back faces
            render_shape(shape);
            glCullFace(GL_BACK);  // Pass 2: Draw front faces
            render_shape(shape);
        }
        else {
            glCullFace(GL_BACK);
            render_shape(shape);
        }
    }
    checkGlErrors("After alpha-test loop"); // <<-- ADD
    shader.setBool("use_alpha_test", false);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    // --- PASS 3: TRANSPARENT (ALPHA-BLEND) OBJECTS ---
    // Render truly transparent objects last, sorted from back to front.
    // They test against the depth buffer but do not write to it to prevent
    // objects behind them from being culled incorrectly.
    if (!transparentShapes.empty()) {
        std::sort(transparentShapes.begin(), transparentShapes.end(),
            [&cameraPos](const MeshShape& a, const MeshShape& b) {
                return glm::distance2(a.boundsCenter, cameraPos) > glm::distance2(b.boundsCenter, cameraPos);
            });

        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE); // CRITICAL: Disable depth writes

        checkGlErrors("Before transparent loop"); // <<-- ADD
        for (const auto& shape : transparentShapes) {
            glBlendFunc(shape.srcBlend, shape.dstBlend);
            if (shape.doubleSided) {
                glDisable(GL_CULL_FACE);
            }
            else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            render_shape(shape);
        }
        checkGlErrors("After transparent loop"); // <<-- ADD
    }

    // --- Reset to default OpenGL state ---
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // Reset the active texture unit to the default before returning.
    // This prevents our function from affecting the state of other renderers (like ImGui).
    glActiveTexture(GL_TEXTURE0);
    checkGlErrors("End of NifModel::draw"); // <<-- ADD
}

void NifModel::drawDepthOnly(Shader& depthShader) {
    depthShader.use();

    GLint boneMatricesLocation = glGetUniformLocation(depthShader.ID, "uBoneMatrices");

    auto render_shape_depth = [&](const MeshShape& shape) {
        if (!shape.visible) return;

        // Don't render shapes that don't receive shadows in the depth pass,
        // as they can't cast them either in this engine.
        if (!shape.receiveShadows) return;

        // Only render shapes that are flagged to cast shadows into the depth map.
        if (!shape.castShadows) return;

        depthShader.setMat4("model", shape.transform);
        depthShader.setBool("uIsSkinned", shape.isSkinned);

        if (shape.isSkinned && !shape.boneMatrices.empty()) {
            GLsizei boneCount = static_cast<GLsizei>(shape.boneMatrices.size());
            if (boneCount > MAX_BONES) boneCount = MAX_BONES;
            glUniformMatrix4fv(boneMatricesLocation, boneCount, GL_FALSE, glm::value_ptr(shape.boneMatrices[0]));
        }
        shape.draw();
        };

    for (const auto& shape : opaqueShapes) render_shape_depth(shape);
    for (const auto& shape : alphaTestShapes) render_shape_depth(shape);
    // Transparent shapes typically do not cast shadows, so they are skipped.
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