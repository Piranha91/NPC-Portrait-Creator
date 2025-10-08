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

std::vector<char> AssetManager::extractFile(const std::string& fileLocation) {
    if (fileLocation.empty()) {
        return {};
    }

    // Case 1: Loose file. The location is a direct filesystem path.
    // We identify this by the absence of the "[bsa_name]" prefix.
    if (fileLocation.find('[') == std::string::npos || fileLocation.front() != '[') {
        std::filesystem::path loosePath = fileLocation;
        if (std::filesystem::exists(loosePath)) {
            std::ifstream file(loosePath, std::ios::binary);
            if (file) {
                // Read the entire file into the vector
                return std::vector<char>((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
            }
        }
        return {}; // File not found on disk or could not be opened.
    }

    // Case 2: BSA file. Location is in "[bsa_name]\relative_path" format.
    size_t bsaEnd = fileLocation.find(']');
    size_t pathStart = fileLocation.find_first_of("\\/", bsaEnd);

    if (bsaEnd == std::string::npos || pathStart == std::string::npos) {
        std::cerr << "Error: Malformed BSA location string: " << fileLocation << std::endl;
        return {};
    }

    std::string bsaName = fileLocation.substr(1, bsaEnd - 1);
    std::string relativePath = fileLocation.substr(pathStart + 1);

    // Find which BsaManager is responsible for this BSA and ask it to extract the file.
    for (auto const& [dirPath, manager] : bsaManagers) {
        if (manager->hasArchive(bsaName)) {
            return manager->extractFile(relativePath);
        }
    }

    // This should not be reached if getFileLocation returned a valid BSA path.
    std::cerr << "Warning: Could not find a manager for BSA '" << bsaName << "'." << std::endl;
    return {};
}

std::string AssetManager::getFileLocation(const std::string& relativePath) {
    // 1. Search for loose files in all active directories, from highest priority to lowest.
    for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
        std::filesystem::path loosePath = *it / relativePath;
        if (std::filesystem::exists(loosePath)) {
            return loosePath.string(); // Return full filesystem path
        }
    }

    // 2. If no loose file was found, search the BSAs for each directory.
    for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
        std::string dirStr = it->string();
        auto managerIt = bsaManagers.find(dirStr);
        if (managerIt != bsaManagers.end()) {
            std::string bsaName = managerIt->second->findFileInArchives(relativePath);
            if (!bsaName.empty()) {
                // Return format: [BSAName]\relativePath
                return "[" + bsaName + "]\\" + relativePath;
            }
        }
    }

    return ""; // Not found
}