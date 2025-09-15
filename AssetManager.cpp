#include "AssetManager.h"
#include <fstream>
#include <iostream>

void AssetManager::setActiveDirectories(const std::vector<std::filesystem::path>& dataDirs, const std::filesystem::path& cacheDir) {
    activeDataDirectories = dataDirs;
    bsaCacheDirectory = cacheDir;

    for (const auto& dir : activeDataDirectories) {
        std::string dirStr = dir.string();
        if (bsaManagers.find(dirStr) == bsaManagers.end()) {
            std::cout << "--- Initializing BSA Manager for: " << dirStr << " ---" << std::endl;
            auto manager = std::make_unique<BsaManager>();
            manager->loadArchives(dirStr, bsaCacheDirectory);
            bsaManagers[dirStr] = std::move(manager);
        }
    }
}

std::vector<char> AssetManager::extractFile(const std::string& relativePath) {
    std::vector<char> fileData;

    // 1. Search for loose files in all active directories, from highest priority to lowest.
    for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
        std::filesystem::path loosePath = *it / relativePath;
        if (std::filesystem::exists(loosePath)) {
            std::ifstream file(loosePath, std::ios::binary);
            fileData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return fileData;
        }
    }

    // 2. If no loose file was found, search the BSAs for each directory.
    for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
        std::string dirStr = it->string();
        auto managerIt = bsaManagers.find(dirStr);
        if (managerIt != bsaManagers.end()) {
            fileData = managerIt->second->extractFile(relativePath);
            if (!fileData.empty()) {
                return fileData;
            }
        }
    }

    return {}; // Return empty vector if not found.
}