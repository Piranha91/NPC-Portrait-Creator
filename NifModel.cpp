#define GLM_ENABLE_EXPERIMENTAL
#include "NifModel.h"
#include "Skeleton.h"
#include "Shader.h"
#include "TextureManager.h" 
#include <iostream>
#include <set>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <Shaders.hpp> 
#include <limits>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/norm.hpp>  // gives length2() and distance2()

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

// --- NEW HELPER FUNCTION: Calculate Tangents and Bitangents ---
void CalculateTangents(std::vector<Vertex>& vertices, const std::vector<nifly::Triangle>& triangles) {
    if (vertices.empty() || triangles.empty()) return;

    // Create temporary storage for tangents and bitangents
    std::vector<glm::vec3> tempTangents(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tempBitangents(vertices.size(), glm::vec3(0.0f));

    // Iterate over all triangles
    for (const auto& tri : triangles) {
        // Get the vertices of the triangle
        Vertex& v0 = vertices[tri.p1];
        Vertex& v1 = vertices[tri.p2];
        Vertex& v2 = vertices[tri.p3];

        // Calculate edges and delta UVs
        glm::vec3 edge1 = v1.pos - v0.pos;
        glm::vec3 edge2 = v2.pos - v0.pos;
        glm::vec2 deltaUV1 = v1.texCoords - v0.texCoords;
        glm::vec2 deltaUV2 = v2.texCoords - v0.texCoords;

        // Calculate tangent and bitangent
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        glm::vec3 tangent, bitangent;

        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        // Accumulate for each vertex
        tempTangents[tri.p1] += tangent;
        tempTangents[tri.p2] += tangent;
        tempTangents[tri.p3] += tangent;
        tempBitangents[tri.p1] += bitangent;
        tempBitangents[tri.p2] += bitangent;
        tempBitangents[tri.p3] += bitangent;
    }

    // Orthonormalize and calculate handedness
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = vertices[i].normal;
        const glm::vec3& t = tempTangents[i];

        // Gram-Schmidt orthonormalize
        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));

        // Calculate handedness (w component)
        float handedness = (glm::dot(glm::cross(n, tangent), tempBitangents[i]) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(tangent, handedness);
    }
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

// --- HELPER FUNCTIONS FROM VERSION 2 ---

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

bool NifModel::load(const std::string& nifPath, TextureManager& textureManager, const Skeleton* skeleton) {
    bool debugMode = true;

    cleanup();
    if (nif.Load(nifPath) != 0) {
        std::cerr << "Error: Failed to load NIF file: " << nifPath << std::endl;
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
    headMinBounds = glm::vec3(std::numeric_limits<float>::max());
    headMaxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    bHasEyeCenter = false;
    nifly::MatTransform skeletonRootTransform; // Default constructs to identity

    // ===================================================================================
    // === HEURISTIC: DETERMINE NIF TYPE (Standard vs. Pre-translated vs. Hybrid)
    // ===================================================================================
    bool useCpuBake = false;
    nifly::MatTransform accessoryOffset;
    nifly::NiShape* headShape = nullptr;

    // --- START: CORRECTED HEURISTIC ---

    // 1. First, check for any pre-translated skinned meshes in the entire file.
    // This catches hybrid models with pre-translated hair/accessories.
    const float PRETRANSLATED_THRESHOLD = 10.0f;
    for (auto* shape : shapeList) {
        if (!shape->IsSkinned()) continue;

        nifly::MatTransform shapeTransform = GetAVObjectTransformToGlobal(nif, shape, false);
        if (shapeTransform.IsNearlyEqualTo(nifly::MatTransform())) {
            const auto* vertices = nif.GetVertsForShape(shape);
            if (vertices && !vertices->empty()) {
                glm::vec3 sum(0.0f);
                for (const auto& v : *vertices) {
                    sum += glm::vec3(v.x, v.y, v.z);
                }
                glm::vec3 centroid = sum / static_cast<float>(vertices->size());
                if (glm::length(centroid) > PRETRANSLATED_THRESHOLD) {
                    useCpuBake = true;
                    if (debugMode) std::cout << "[NIF Analysis] DECISION: Hybrid model with pre-translated parts detected (Shape: " << shape->name.get() << "). Forcing CPU Bake strategy.\n";
                    break;
                }
            }
        }
    }

    // 2. If no hybrid parts were found, proceed with the original head-based analysis.
    if (!useCpuBake) {
        // Find the main head shape
        for (auto* shape : shapeList) {
            if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(shape->SkinInstanceRef())) {
                for (const auto& partition : skinInst->partitions) {
                    if (partition.partID == 30 || partition.partID == 230) {
                        headShape = shape;
                        break;
                    }
                }
            }
            if (headShape) break;
        }

        if (headShape) {
            if (debugMode) std::cout << "[NIF Analysis] Found main head part: '" << headShape->name.get() << "'\n";
            nifly::MatTransform headNodeTransform = GetAVObjectTransformToGlobal(nif, headShape, false);
            const auto* vertices = nif.GetVertsForShape(headShape);
            glm::vec3 headLocalCentroid(0.0f);
            if (vertices && !vertices->empty()) {
                glm::vec3 sum(0.0f);
                for (const auto& v : *vertices) { sum += glm::vec3(v.x, v.y, v.z); }
                headLocalCentroid = sum / static_cast<float>(vertices->size());
            }

            if (debugMode) {
                std::cout << "[NIF Analysis] Head Node Transform is " << (headNodeTransform.IsNearlyEqualTo(nifly::MatTransform()) ? "Identity" : "Not Identity") << ".\n";
                std::cout << "[NIF Analysis] Head Local Centroid length is " << glm::length(headLocalCentroid) << ".\n";
            }

            // This case handles fully pre-translated head NIFs
            if (headNodeTransform.IsNearlyEqualTo(nifly::MatTransform()) && glm::length(headLocalCentroid) > PRETRANSLATED_THRESHOLD) {
                useCpuBake = true;
                std::cout << "[NIF Analysis] DECISION: Pre-translated model detected. Using CPU Bake strategy.\n";
            }
            else {
                useCpuBake = false;
                std::cout << "[NIF Analysis] DECISION: Standard model detected. Using GPU Transform strategy.\n";
                accessoryOffset = headNodeTransform;
            }
        }
        else {
            useCpuBake = true;
            std::cout << "[NIF Analysis] No head part found. Defaulting to CPU Bake strategy.\n";
        }
    }

    // ===================================================================================
    // === PROCESS ALL SHAPES USING THE CHOSEN STRATEGY
    // ===================================================================================
    for (auto* niShape : shapeList) {
        if (debugMode) std::cout << "\n--- Processing Shape: " << niShape->name.get() << " ---\n";
        if (!niShape || (niShape->flags & 1)) continue;

        const auto* vertices = nif.GetVertsForShape(niShape);
        if (!vertices || vertices->empty()) continue;

        std::vector<Vertex> vertexData(vertices->size());
        const auto* colors = nif.GetColorsForShape(niShape->name.get()); // FIX: Get vertex colors

        for (size_t i = 0; i < vertices->size(); ++i) {
            vertexData[i].pos = glm::vec3((*vertices)[i].x, (*vertices)[i].y, (*vertices)[i].z);
            const auto* normals = nif.GetNormalsForShape(niShape);
            if (normals && i < normals->size()) vertexData[i].normal = glm::vec3((*normals)[i].x, (*normals)[i].y, (*normals)[i].z);
            else vertexData[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
            const auto* uvs = nif.GetUvsForShape(niShape);
            if (uvs && i < uvs->size()) vertexData[i].texCoords = glm::vec2((*uvs)[i].u, (*uvs)[i].v);
            else vertexData[i].texCoords = glm::vec2(0.0f, 0.0f);

            // FIX: Store vertex color, defaulting to white if not present
            if (colors && i < colors->size()) {
                const auto& c = (*colors)[i];
                vertexData[i].color = glm::vec4(c.r, c.g, c.b, c.a);
            }
            else {
                vertexData[i].color = glm::vec4(1.0f);
            }
        }

        MeshShape mesh;
        std::string shapeName = niShape->name.get();

        // Safely check for eye data via dynamic_cast
        if (const auto* triShape = dynamic_cast<const nifly::BSTriShape*>(niShape)) {
            if (triShape->HasEyeData()) {
                std::cout << "  [Eye Detection] SUCCESS: Shape '" << shapeName << "' has the VF_EYEDATA flag and is tagged as an eye." << std::endl;
                mesh.isEye = true;
            }
            else {
                mesh.isEye = false;
                std::cout << "  [Eye Detection] INFO: Shape '" << shapeName << "' does not have the VF_EYEDATA flag." << std::endl;
			}
        }
        else
        {
            std::cout << "  [Eye Detection] FAIL: Shape '" << shapeName << "' cannot be cast to BSTriShape." << std::endl;
        }

        if (useCpuBake) {
            // --- STRATEGY 2: CPU Vertex Bake (For Pre-translated NIFs like NPC #2) ---
            auto* skinInst = niShape->IsSkinned() ? nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef()) : nullptr;
            if (skinInst) {
                auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef);
                auto* skinPartition = nif.GetHeader().GetBlock<nifly::NiSkinPartition>(skinInst->skinPartitionRef);

                if (skinData && skinPartition) {
                    if (debugMode) std::cout << "    [Debug] Baking skinned vertices on CPU...\n";

                    // This vector will hold the final transform for each bone.
                    std::vector<glm::mat4> boneWorldMatrices(skinData->bones.size());
                    auto boneRefIt = skinInst->boneRefs.begin();

                    for (size_t i = 0; i < skinData->bones.size() && boneRefIt != skinInst->boneRefs.end(); ++i, ++boneRefIt) {
                        auto* boneNode = nif.GetHeader().GetBlock<nifly::NiNode>(*boneRefIt);
                        if (!boneNode) continue;

                        std::string boneName = boneNode->name.get();
                        glm::mat4 boneWorld;

                        // --- THE KEY CHANGE IS HERE ---
                        if (debugMode) std::cout << "      [Debug] Looking up bone '" << boneName << "' in active skeleton...\n";
                        if (skeleton && skeleton->hasBone(boneName)) {
                            boneWorld = skeleton->getBoneTransform(boneName);
                            if (debugMode) std::cout << "      [Debug] Found bone '" << boneName << "' in loaded skeleton.\n";
                        }
                        else {
                            // Fallback to the old method if bone isn't in skeleton or if no skeleton is loaded.
                            auto boneWorld_n = GetAVObjectTransformToGlobal(nif, boneNode, false);
                            boneWorld = glm::transpose(glm::make_mat4(&boneWorld_n.ToMatrix()[0]));
                            if (debugMode) std::cout << "      [Debug] WARN: Bone '" << boneName << "' not in skeleton. Using transform from NIF.\n";
                        }

                        // Get the skin-to-bone transform from the NIF's skin data.
                        const auto& skinToBone_n = skinData->bones[i].boneTransform;
                        glm::mat4 skinToBone = glm::transpose(glm::make_mat4(&skinToBone_n.ToMatrix()[0]));

                        // The final matrix for each vertex is (BoneWorld * SkinToBone)
                        boneWorldMatrices[i] = boneWorld * skinToBone;
                    }

                    // --- The rest of the vertex baking logic remains the same ---
                    auto originalVertexData = vertexData;
                    std::fill(vertexData.begin(), vertexData.end(), Vertex{});
                    std::vector<float> totalWeights(originalVertexData.size(), 0.0f);

                    for (const auto& partition : skinPartition->partitions) {
                        if (!partition.hasVertexMap || !partition.hasVertexWeights || !partition.hasBoneIndices) continue;
                        for (uint16_t i = 0; i < partition.numVertices; ++i) {
                            uint16_t originalVertIndex = partition.vertexMap[i];
                            if (originalVertIndex >= originalVertexData.size()) continue;

                            const float weights[] = { partition.vertexWeights[i].w1, partition.vertexWeights[i].w2, partition.vertexWeights[i].w3, partition.vertexWeights[i].w4 };
                            const uint8_t indices[] = { partition.boneIndices[i].i1, partition.boneIndices[i].i2, partition.boneIndices[i].i3, partition.boneIndices[i].i4 };

                            for (uint16_t k = 0; k < partition.numWeightsPerVertex; ++k) {
                                float weight = weights[k];
                                if (weight <= 0.0f) continue;
                                uint16_t globalBoneIndex = partition.bones[indices[k]];
                                if (globalBoneIndex >= boneWorldMatrices.size()) continue;

                                const glm::mat4& boneMatrix = boneWorldMatrices[globalBoneIndex];
                                vertexData[originalVertIndex].pos += glm::vec3(boneMatrix * glm::vec4(originalVertexData[originalVertIndex].pos, 1.0f)) * weight;

                                glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(boneMatrix)));
                                vertexData[originalVertIndex].normal += (normalMatrix * originalVertexData[originalVertIndex].normal) * weight;

                                totalWeights[originalVertIndex] += weight;
                            }
                            vertexData[originalVertIndex].texCoords = originalVertexData[originalVertIndex].texCoords;
                            vertexData[originalVertIndex].color = originalVertexData[originalVertIndex].color; // FIX: Preserve vertex color
                        }
                    }

                    for (size_t i = 0; i < vertexData.size(); ++i) {
                        if (totalWeights[i] > 0.0f) {
                            vertexData[i].pos /= totalWeights[i];
                            vertexData[i].normal = glm::normalize(vertexData[i].normal);
                        }
                        else {
                            // If a vertex has no weights, just use its original data.
                            vertexData[i] = originalVertexData[i];
                        }
                    }

                    // Since vertices are now in their final world positions, the mesh's transform is identity.
                    mesh.transform = glm::mat4(1.0f);
                }
                else {
                    // If there's no skin data, fall back to the object's scene graph transform.
                    mesh.transform = glm::transpose(glm::make_mat4(&GetAVObjectTransformToGlobal(nif, niShape).ToMatrix()[0]));
                }
            }
            else { // Unskinned shape in a CPU bake model
                mesh.transform = glm::transpose(glm::make_mat4(&GetAVObjectTransformToGlobal(nif, niShape).ToMatrix()[0]));
            }
        }
        else {
            // --- STRATEGY 1: GPU Transform (For Standard NIFs like NPC #1) ---
            nifly::MatTransform niflyTransform;
            nifly::MatTransform ownTransform = GetAVObjectTransformToGlobal(nif, niShape, false);

            // Default to using the shape's own transform from the scene graph.
            niflyTransform = ownTransform;

            // Heuristic: If a face part has an identity transform, it's likely "broken"
            // and needs to be manually placed using the head's offset. This is common
            // for eyes, mouth, and brows as your analysis pointed out.
            if (ownTransform.IsNearlyEqualTo(nifly::MatTransform())) {
                bool isLocalSpacePart = (
                    shapeName.find("Eyes") != std::string::npos ||
                    shapeName.find("Mouth") != std::string::npos ||
                    shapeName.find("Teeth") != std::string::npos || // Added for clarity
                    shapeName.find("Brows") != std::string::npos
                    );

                // This handles the "5 feet above the head" case for hair/scalps that
                // were exported at the origin without a transform.
                if (!isLocalSpacePart) {
                    glm::vec3 localCentroid = CalculateCentroid(vertexData);
                    const float LOCAL_SPACE_THRESHOLD = 1.0f; // A small threshold
                    if (glm::length(localCentroid) < LOCAL_SPACE_THRESHOLD) {
                        if (debugMode) std::cout << "    [Debug] Shape '" << shapeName << "' is at origin with no transform. Applying accessory offset as a fix.\n";
                        isLocalSpacePart = true;
                    }
                }

                if (isLocalSpacePart) {
                    if (debugMode) std::cout << "    [Debug] Applying accessory offset for local-space part '" << shapeName << "'.\n";
                    niflyTransform = accessoryOffset;
                }
                else {
                    // FIX: Pre-translated parts are not absolute; they are relative to the
                    // skeleton root. Apply the skeleton root's transform to move them
                    // into their final world position along with the rest of the rig.
                    if (debugMode) std::cout << "    [Debug] Applying skeleton root transform for pre-translated part '" << shapeName << "'.\n";
                    niflyTransform = skeletonRootTransform;
                }
            }
            else {
                if (debugMode) std::cout << "    [Debug] Using shape's own scene graph transform for '" << shapeName << "'.\n";
            }

            mesh.transform = glm::transpose(glm::make_mat4(&niflyTransform.ToMatrix()[0]));

            // --- RESTORED LOGGING BLOCK ---
            glm::vec3 originalCentroid = CalculateCentroid(vertexData);
            auto* skinInst = niShape->IsSkinned() ? nif.GetHeader().GetBlock<nifly::NiSkinInstance>(niShape->SkinInstanceRef()) : nullptr;
            if (debugMode) {
                std::cout << "    [Debug] Calculated world transform. Applying to mesh.\n";
                glm::vec3 transformedCentroid = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
                std::cout << "    [Debug] --- Mesh Transformation (" << (skinInst ? "Skinned as Rigid" : "Unskinned") << ") ---\n";
                std::cout << "    [Debug] Original Centroid: " << glm::to_string(originalCentroid) << "\n";
                std::cout << "    [Debug] Transformation Matrix:\n" << glm::to_string(mesh.transform) << "\n";
                std::cout << "    [Debug] Transformed Centroid: " << glm::to_string(transformedCentroid) << "\n";
                std::cout << "    [Debug] ------------------------------------------\n";
            }
        }

        // --- COMMON LOGIC FOR BOTH STRATEGIES ---

        if (shapeName.find("Eyes") != std::string::npos) {
            glm::vec3 originalCentroid = CalculateCentroid(vertexData);
            eyeCenter = glm::vec3(mesh.transform * glm::vec4(originalCentroid, 1.0f));
            bHasEyeCenter = true;
        }

        const nifly::NiShader* shader = nif.GetShader(niShape);
        if (shader) {
            mesh.isModelSpace = shader->IsModelSpace();
        }

        // Bounds calculation
        glm::mat4 boundsTransform = mesh.transform;

        // Calculate per-shape bounds
        glm::vec3 shapeMinBounds(std::numeric_limits<float>::max());
        glm::vec3 shapeMaxBounds(std::numeric_limits<float>::lowest());

        // --- FIX: Exclude accessories by partition ID, with a name-based fallback ---
        bool isAccessoryPart = false;

        // 1. Prioritize checking the body part partition. This is the most reliable method for standard assets.
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::BSDismemberSkinInstance>(niShape->SkinInstanceRef())) {
            for (const auto& partition : skinInst->partitions) {
                // SBP_131_HAIR is the standard partition for hair, helmets, and other head accessories.
                if (partition.partID == 131) {
                    isAccessoryPart = true;
                    break; // Found a hair partition, no need to check further for this shape.
                }
            }
        }

        // 2. Fallback to a name-based check for assets that might not use partitions correctly
        //    (e.g., older mods or unskinned hair meshes).
        if (!isAccessoryPart) {
            std::string lowerShapeName = shapeName;
            std::transform(lowerShapeName.begin(), lowerShapeName.end(), lowerShapeName.begin(), ::tolower);
            if (lowerShapeName.find("hair") != std::string::npos || lowerShapeName.find("scalp") != std::string::npos) {
                isAccessoryPart = true;
            }
        }

        for (const auto& vert : vertexData) {
            glm::vec3 tv3 = glm::vec3(boundsTransform * glm::vec4(vert.pos, 1.0f));
            // Update model-total bounds
            minBounds = glm::min(minBounds, tv3);
            maxBounds = glm::max(maxBounds, tv3);

            // Update per-shape bounds
            shapeMinBounds = glm::min(shapeMinBounds, tv3);
            shapeMaxBounds = glm::max(shapeMaxBounds, tv3);

            // Only include vertices in head bounds if they are not part of an accessory mesh
            if (!isAccessoryPart) {
                headMinBounds = glm::min(headMinBounds, tv3);
                headMaxBounds = glm::max(headMaxBounds, tv3);
            }
        }

        // Store the calculated center for this shape
        mesh.boundsCenter = (shapeMinBounds + shapeMaxBounds) * 0.5f;

        // Texture and material property loading (same for both paths)
        if (shader && shader->HasTextureSet()) {
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                for (size_t i = 0; i < textureSet->textures.size(); ++i) {
                    std::string texPath = textureSet->textures[i].get();
                    if (texPath.empty()) continue;

                    // NEW: Determine if the texture slot contains color data
                    bool isColor = false;
                    switch (i) {
                    case 0: // Diffuse
                    case 6: // Face Tint Mask
                        isColor = true;
                        break;
                    default: // Normal, Skin, Specular, Detail, etc.
                        isColor = false;
                        break;
                    }

                    switch (i) {
                    case 0: mesh.diffuseTextureID = textureManager.loadTexture(texPath, isColor); break;
                    case 1: mesh.normalTextureID = textureManager.loadTexture(texPath, isColor); break;
                    case 2: mesh.skinTextureID = textureManager.loadTexture(texPath, isColor); break;
                    case 3: mesh.detailTextureID = textureManager.loadTexture(texPath, isColor); break;
                    case 6: mesh.faceTintColorMaskID = textureManager.loadTexture(texPath, isColor); break;
                    case 7: mesh.specularTextureID = textureManager.loadTexture(texPath, isColor); break;
                    default: break;
                    }
                }
            }
        }
        if (const auto* bslsp = dynamic_cast<const nifly::BSLightingShaderProperty*>(shader)) {
            mesh.useVertexColors = (bslsp->shaderFlags1 & (1U << 12));
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

        // VAO/VBO/EBO setup (same for both paths)
        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);
        glBindVertexArray(mesh.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(Vertex), vertexData.data(), GL_STATIC_DRAW);

        std::vector<nifly::Triangle> triangles;
        niShape->GetTriangles(triangles);

        // --- Calculate tangents after loading vertex data ---
        bool hasTangentSpaceNormalMap = false;
        if (shader && shader->HasTextureSet() && !shader->IsModelSpace()) {
            // Get the texture set associated with the shader
            if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                // The normal map is in texture slot 1. Check if it exists and has a non-empty path.
                if (textureSet->textures.size() > 1 && !textureSet->textures[1].get().empty()) {
                    hasTangentSpaceNormalMap = true;
                }
            }
        }

        if (hasTangentSpaceNormalMap) {
            CalculateTangents(vertexData, triangles);
        }

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

        // FIX: Add vertex attribute for color
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

        // --- Add vertex attribute for tangent ---
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));


        glBindVertexArray(0);

        // Fix: 3 pass rendering depending on transparency and alpha testing
        if (mesh.hasAlphaProperty) {
            if (mesh.alphaBlend) {
                transparentShapes.push_back(mesh);
            }
            else if (mesh.alphaTest) {
                alphaTestShapes.push_back(mesh);
            }
            else {
                opaqueShapes.push_back(mesh);
            }
        }
        else {
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

    // Set eye-specific shader parameters
    shader.setFloat("eye_fresnel_strength", 0.3f);
    shader.setFloat("eye_spec_power", 80.0f);

    // --- PASS 1: OPAQUE OBJECTS ---
    // Render all fully opaque objects first. They will populate the depth buffer.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (const auto& shape : opaqueShapes) {
        shader.setMat4("model", shape.transform);
        shader.setBool("is_eye", shape.isEye);
        shader.setBool("use_vertex_colors", shape.useVertexColors);
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

    // --- PASS 2: ALPHA-TEST (CUTOUT) OBJECTS ---
    // Render cutout objects next. They test against the depth buffer and also write to it.
    // This allows different cutout objects (e.g., hair, eyebrows, eyelashes) to correctly occlude each other.

    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); // Enable to improve anti-aliasing on hair edges

    for (const auto& shape : alphaTestShapes) {
        shader.setMat4("model", shape.transform);
        shader.setBool("use_vertex_colors", shape.useVertexColors);
        shader.setBool("has_tint_color", shape.hasTintColor);
        if (shape.hasTintColor) {
            shader.setVec3("tint_color", shape.tintColor);
        }
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);
        if (shape.normalTextureID != 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shape.normalTextureID); }
        if (shape.skinTextureID != 0) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shape.skinTextureID); }
        if (shape.detailTextureID != 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, shape.detailTextureID); }
        if (shape.specularTextureID != 0) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shape.specularTextureID); }

        shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
        if (shape.faceTintColorMaskID != 0) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID);
        }

        shader.setBool("use_alpha_test", true);
        shader.setFloat("alpha_threshold", shape.alphaThreshold);

        if (shape.doubleSided) {
            glCullFace(GL_FRONT); // Pass 1: Draw back faces
            shape.draw();
            glCullFace(GL_BACK);  // Pass 2: Draw front faces
            shape.draw();
        }
        else {
            glCullFace(GL_BACK);
            shape.draw();
        }
    }
    shader.setBool("use_alpha_test", false);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    // --- PASS 3: TRANSPARENT (ALPHA-BLEND) OBJECTS ---
    // Render truly transparent objects last, sorted from back to front.
    // They test against the depth buffer but do not write to it.
    if (!transparentShapes.empty()) {
        // CORRECTED: Sort by the distance to the mesh's calculated bounds center
        std::sort(transparentShapes.begin(), transparentShapes.end(),
            [&cameraPos](const MeshShape& a, const MeshShape& b) {
                return glm::distance2(a.boundsCenter, cameraPos) > glm::distance2(b.boundsCenter, cameraPos);
            });

        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE); // CRITICAL: Disable depth writes

        for (const auto& shape : transparentShapes) {
            shader.setMat4("model", shape.transform);
            shader.setBool("use_vertex_colors", shape.useVertexColors);
            shader.setBool("has_tint_color", shape.hasTintColor);
            if (shape.hasTintColor) shader.setVec3("tint_color", shape.tintColor);

            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shape.diffuseTextureID);
            if (shape.normalTextureID != 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, shape.normalTextureID); }
            if (shape.skinTextureID != 0) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shape.skinTextureID); }
            if (shape.detailTextureID != 0) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, shape.detailTextureID); }
            if (shape.specularTextureID != 0) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shape.specularTextureID); }

            shader.setBool("has_face_tint_map", shape.faceTintColorMaskID != 0);
            if (shape.faceTintColorMaskID != 0) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, shape.faceTintColorMaskID);
            }

            glBlendFunc(shape.srcBlend, shape.dstBlend);
            if (shape.doubleSided) {
                glDisable(GL_CULL_FACE);
            }
            else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }

            shape.draw();
        }
    }

    // --- Reset to default OpenGL state after drawing all shapes ---
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void NifModel::cleanup() {
    for (auto& shape : opaqueShapes) {
        shape.cleanup();
    }
    opaqueShapes.clear();

    for (auto& shape : alphaTestShapes) {
        shape.cleanup();
    }
    alphaTestShapes.clear();

    for (auto& shape : transparentShapes) {
        shape.cleanup();
    }
    transparentShapes.clear();

    texturePaths.clear();
}

std::vector<std::string> NifModel::getTextures() const {
    return texturePaths;
}
