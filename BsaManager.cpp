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
                libbsarch::bs_archive bsa;
                bsa.load_from_disk(bsaPath.wstring());
                const std::string bsaFilename = bsaPath.filename().string();

                for (const auto& file : bsa.list_files()) {
                    std::string filePath = std::string(file);
                    std::replace(filePath.begin(), filePath.end(), '/', '\\');
                    std::transform(filePath.begin(), filePath.end(), filePath.begin(), ::tolower);

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


size_t BsaManager::getArchiveCount() const {
    return bsaPaths.size();
}

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

// This function now correctly finds the full BSA path from its internal list.
std::vector<char> BsaManager::extractFile(const std::string& relativePath) const {
    std::string bsaName = findFileInArchives(relativePath);
    if (bsaName.empty()) {
        return {};
    }

    // Find the full path to the BSA from the stored list.
    std::filesystem::path bsaFullPath;
    for (const auto& p : bsaPaths) {
        if (p.filename().string() == bsaName) {
            bsaFullPath = p;
            break;
        }
    }

    if (bsaFullPath.empty()) {
        return {}; // Should not happen if findFileInArchives worked, but good for safety.
    }

    try {
        libbsarch::bs_archive bsa;
        bsa.load_from_disk(bsaFullPath.wstring());

        libbsarch::memory_blob blob = bsa.extract_to_memory(relativePath);
        const char* data = static_cast<const char*>(blob.data);
        return std::vector<char>(data, data + blob.size);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to extract " << relativePath << " from " << bsaName << ": " << e.what() << std::endl;
        return {};
    }
}