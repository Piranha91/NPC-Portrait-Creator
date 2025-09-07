#include "BsaManager.h"
#include <iostream>
#include <algorithm>

// Include the correct C++ wrapper header for reading archives
#include <bs_archive.h>

void BsaManager::loadArchives(const std::string& directory) {
    bsaPaths.clear();
    fileCache.clear();

    if (directory.empty() || !std::filesystem::exists(directory)) {
        return;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                if (extension == ".bsa") {
                    bsaPaths.push_back(entry.path());
                }
            }
        }
        std::sort(bsaPaths.begin(), bsaPaths.end());

        std::cout << "--- Caching BSA contents from: " << directory << " ---" << std::endl;
        for (const auto& bsaPath : bsaPaths) {
            try {
                // 1. Create the correct bs_archive object.
                libbsarch::bs_archive bsa;

                // 2. Load the archive from disk.
                bsa.load_from_disk(bsaPath.wstring());
                const std::string bsaFilename = bsaPath.filename().string();

                // 3. Use the list_files() method to get all file paths.
                for (const auto& file : bsa.list_files()) {
                    std::string filePath = std::string(file);

                    // Normalize the path for consistent lookups
                    std::replace(filePath.begin(), filePath.end(), '/', '\\');
                    std::transform(filePath.begin(), filePath.end(), filePath.begin(), ::tolower);

                    // Omit paths from the debug output that are unlikely to feature in a character's face
                    if (filePath.find("texture") != std::string::npos && 
                        filePath.find("terrain") == std::string::npos &&
                        filePath.find("clutter") == std::string::npos &&
                        filePath.find("architecture") == std::string::npos &&
                        filePath.find("weapons") == std::string::npos &&
                        filePath.find("armor") == std::string::npos &&
                        filePath.find("clothes") == std::string::npos &&
                        filePath.find("landscape") == std::string::npos &&
                        filePath.find("dungeon") == std::string::npos &&
                        filePath.find("effects") == std::string::npos)
                    {
                        std::cout << "[" << bsaFilename << "]: " << filePath << std::endl;
                    }

                    // Add the file to our cache
                    fileCache[filePath] = bsaFilename;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error processing BSA " << bsaPath.string() << ": " << e.what() << std::endl;
            }
        }
        std::cout << "--- BSA Caching Complete ---" << std::endl;

    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning directory for archives: " << e.what() << std::endl;
    }
}

// This function remains correct and does not need changes.
std::string BsaManager::findFileInArchives(const std::string& relativePath) const {
    if (relativePath.empty()) {
        return "";
    }

    std::string internalPath = relativePath;
    std::replace(internalPath.begin(), internalPath.end(), '/', '\\');
    std::transform(internalPath.begin(), internalPath.end(), internalPath.begin(), ::tolower);

    auto it = fileCache.find(internalPath);
    if (it != fileCache.end()) {
        return it->second;
    }

    return "";
}

size_t BsaManager::getArchiveCount() const {
    return bsaPaths.size();
}