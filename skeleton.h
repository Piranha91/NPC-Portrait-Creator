#pragma once

#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <NifFile.hpp>
#include <Nodes.hpp>

// Helper to convert nifly's matrix to GLM's, including the necessary transpose.
inline glm::mat4 NiflyToGlm(const nifly::MatTransform& niflyMat) {
    return glm::transpose(glm::make_mat4(&niflyMat.ToMatrix()[0]));
}

class Skeleton {
public:
    Skeleton() = default;

    bool loadFromFile(const std::string& path);
    bool loadFromMemory(const std::vector<char>& buffer, const std::string& name);

    glm::mat4 getBoneTransform(const std::string& boneName) const;
    bool hasBone(const std::string& boneName) const;
    bool isLoaded() const { return !boneWorldTransforms.empty(); }
    void clear();

private:
    void parseNif();
    void processNode(nifly::NiNode* node, const nifly::MatTransform& parentTransform);

    nifly::NifFile nif;
    std::map<std::string, glm::mat4> boneWorldTransforms;
};