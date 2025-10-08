#pragma once

#include "BsaManager.h"
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <memory>

class AssetManager {
public:
    AssetManager() = default;

    void setActiveDirectories(const std::vector<std::filesystem::path>& dataDirs, const std::filesystem::path& cacheDir);
    void setGameDataDirectory(const std::filesystem::path& gameDataDir);
    std::vector<char> extractFile(const std::string& fileLocation);
    std::string getFileLocation(const std::string& relativePath, bool baseGameTextureFallbackOnly = false);

private:
    std::vector<std::filesystem::path> activeDataDirectories;
    std::filesystem::path gameDataDirectory;
    std::map<std::string, std::unique_ptr<BsaManager>> bsaManagers;
    std::filesystem::path bsaCacheDirectory;
};