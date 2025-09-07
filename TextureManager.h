#pragma once

#include "BsaManager.h"
#include <string>

class TextureManager {
public:
    TextureManager() = default;
    void setActiveDirectories(const std::string& rootDir, const std::string& fallbackDir);
    void checkTexture(const std::string& relativePath) const;

private:
    std::string rootDirectory;
    std::string fallbackRootDirectory;
    BsaManager rootBsaManager;
    BsaManager fallbackBsaManager;
};