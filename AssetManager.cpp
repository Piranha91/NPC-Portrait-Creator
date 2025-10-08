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

void AssetManager::setGameDataDirectory(const std::filesystem::path& gameDataDir) {
    gameDataDirectory = gameDataDir;
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

std::string AssetManager::getFileLocation(const std::string& relativePath, bool baseGameTextureFallbackOnly) {
    // --- PATH 1: Standard Search (original behavior) ---
    if (!baseGameTextureFallbackOnly) {
        // 1. Search for loose files in all active directories, from highest priority to lowest.
        for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
            std::filesystem::path loosePath = *it / relativePath;
            if (std::filesystem::exists(loosePath)) {
                return loosePath.string();
            }
        }

        // 2. If no loose file was found, search all BSAs for each directory.
        for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
            std::string dirStr = it->string();
            auto managerIt = bsaManagers.find(dirStr);
            if (managerIt != bsaManagers.end()) {
                std::string bsaName = managerIt->second->findFileInArchives(relativePath);
                if (!bsaName.empty()) {
                    return "[" + bsaName + "]\\" + relativePath;
                }
            }
        }
        return ""; // Not found
    }

    // --- PATH 2: Restricted Fallback Search (for face tint) ---
    else {
        // 1. Search NON-BASE game directories first (mods). This includes loose files and all BSAs.
        for (auto it = activeDataDirectories.rbegin(); it != activeDataDirectories.rend(); ++it) {
            if (*it == gameDataDirectory) {
                continue; // Skip the base game directory for this first pass.
            }
            // Check for loose files in the mod directory.
            std::filesystem::path loosePath = *it / relativePath;
            if (std::filesystem::exists(loosePath)) {
                return loosePath.string();
            }
            // Check for BSAs in the mod directory.
            std::string dirStr = it->string();
            auto managerIt = bsaManagers.find(dirStr);
            if (managerIt != bsaManagers.end()) {
                std::string bsaName = managerIt->second->findFileInArchives(relativePath);
                if (!bsaName.empty()) {
                    return "[" + bsaName + "]\\" + relativePath;
                }
            }
        }

        // 2. If not found in mods, perform the restricted search on the base game directory.
        if (!gameDataDirectory.empty()) {
            auto managerIt = bsaManagers.find(gameDataDirectory.string());
            if (managerIt != bsaManagers.end()) {
                // Build the list of allowed vanilla BSAs.
                std::vector<std::string> vanillaBsaNames;
                for (int i = 0; i <= 8; ++i) {
                    vanillaBsaNames.push_back("Skyrim - Textures" + std::to_string(i) + ".bsa");
                }
                // Add any Creation Club BSAs (names starting with "cc").
                for (const auto& bsaPath : managerIt->second->getBsaPaths()) {
                    std::string filename = bsaPath.filename().string();
                    if (filename.size() > 2 && _strnicmp(filename.c_str(), "cc", 2) == 0) {
                        vanillaBsaNames.push_back(filename);
                    }
                }

                // Perform the search using only the allowed BSA list.
                std::string bsaName = managerIt->second->findFileInArchives(relativePath, vanillaBsaNames);
                if (!bsaName.empty()) {
                    return "[" + bsaName + "]\\" + relativePath;
                }
            }
        }

        return ""; // Not found after restricted search.
    }
}