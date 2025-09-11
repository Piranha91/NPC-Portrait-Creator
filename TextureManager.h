// TextureManager.h
#pragma once

#include "BsaManager.h"
#include <string>
#include <unordered_map>
#include <glad/glad.h>

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();

    void setActiveDirectories(const std::string& rootDir, const std::string& fallbackDir);
    // Add a boolean parameter to indicate if the texture contains color data
    GLuint loadTexture(const std::string& relativePath, bool isColorData);

private:
    // Pass the boolean down to the upload function
    GLuint uploadDDSToGPU(const std::vector<char>& data, const std::string& texturePath, bool isColorData);
    void cleanup();

    std::string rootDirectory;
    std::string fallbackRootDirectory;
    BsaManager rootBsaManager;
    BsaManager fallbackBsaManager;

    std::unordered_map<std::string, GLuint> textureCache;
};