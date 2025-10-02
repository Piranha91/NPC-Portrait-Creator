#include "Skeleton.h"
#include <iostream>
#include <sstream>
#include <iomanip> // For std::setw, std::setprecision
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include "CommonMatrices.h"

void Skeleton::clear() {
    boneWorldTransforms.clear();
}

bool Skeleton::loadFromFile(const std::string& path) {
    clear();
    if (nif.Load(path) != 0) {
        std::cerr << "Error: Failed to load skeleton file: " << path << std::endl;
        return false;
    }
    std::cout << "Successfully loaded skeleton: " << path << std::endl;
    parseNif();
    return true;
}

bool Skeleton::loadFromMemory(const std::vector<char>& buffer, const std::string& name) {
    clear();

    // Use a stringstream to create an istream from the memory buffer
    std::stringstream ss(std::string(buffer.begin(), buffer.end()));

    if (nif.Load(ss) != 0) { // Call Load with the stringstream
        std::cerr << "Error: Failed to load skeleton from memory: " << name << std::endl;
        return false;
    }

    std::cout << "Successfully loaded skeleton from BSA: " << name << std::endl;
    parseNif();
    return true;
}

void Skeleton::parseNif() {
    auto* root = nif.GetRootNode();
    if (root) {
        processNode(root, nifly::MatTransform()); // Start with identity parent transform
    }
}

void Skeleton::processNode(nifly::NiNode* node, const nifly::MatTransform& parentTransform) {
    if (!node) {
        return;
    }

    nifly::MatTransform localTransform = node->transform;
    nifly::MatTransform worldTransform = parentTransform.ComposeTransforms(localTransform);

    std::string nodeName = node->name.get();
    if (!nodeName.empty()) {
        glm::mat4 glmWorldTransform = Matrices::NiflyToGlm(worldTransform);
        boneWorldTransforms[nodeName] = glmWorldTransform;

        std::cout << "    [Skel Parse] Stored transform for bone: " << nodeName << std::endl;

        std::stringstream niflyMatrixStream;
        nifly::Matrix4 tempMat = worldTransform.ToMatrix();
        const float* matrixData = &tempMat[0];
        niflyMatrixStream << "\n";
        for (int row = 0; row < 4; ++row) {
            niflyMatrixStream << "            [";
            for (int col = 0; col < 4; ++col) {
                niflyMatrixStream << std::setw(9) << std::fixed << std::setprecision(4) << matrixData[row * 4 + col];
                if (col < 3) niflyMatrixStream << ", ";
            }
            niflyMatrixStream << "]\n";
        }
        std::cout << "        [Skel Parse] Nifly Matrix (Z-up, Row-major):" << niflyMatrixStream.str();
        std::cout << "        [Skel Parse] GLM Matrix (Z-up, Col-major):\n" << glm::to_string(glmWorldTransform) << std::endl;
    }

    for (const auto& childRef : node->childRefs) { // Corrected to 'childRefs'
        if (auto* childNode = nif.GetHeader().GetBlock<nifly::NiNode>(childRef)) {
            processNode(childNode, worldTransform);
        }
    }
}

glm::mat4 Skeleton::getBoneTransform(const std::string& boneName) const {
    auto it = boneWorldTransforms.find(boneName);
    if (it != boneWorldTransforms.end()) {
        return it->second;
    }
    return glm::mat4(1.0f); // Return identity if not found
}

bool Skeleton::hasBone(const std::string& boneName) const {
    return boneWorldTransforms.count(boneName) > 0;
}