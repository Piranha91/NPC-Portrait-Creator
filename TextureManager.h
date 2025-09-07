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
    GLuint loadTexture(const std::string& relativePath);

private:
    GLuint uploadDDSToGPU(const std::vector<char>& data);
    void cleanup();

    std::string rootDirectory;
    std::string fallbackRootDirectory;
    BsaManager rootBsaManager;
    BsaManager fallbackBsaManager;

    std::unordered_map<std::string, GLuint> textureCache;
};