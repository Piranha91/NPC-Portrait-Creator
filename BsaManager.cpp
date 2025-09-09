#include "BsaManager.h"
#include <iostream>
#include <algorithm>

// Include the correct C++ wrapper header for reading archives
#include <bs_archive.h>

void BsaManager::loadArchives(const std::string& directory) {
	bool showDebug = false; // Set to true to enable debug output
    bsaPaths.clear();
    anyCache.clear();
    texturesCache.clear();
    meshesCache.clear();

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
                    std::string filePath = normalizePath(std::string(file));

					if (showDebug &&
                        filePath.find("texture") != std::string::npos &&
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

                    // Route to the correct cache by top-level folder
                    if (filePath.rfind("textures\\", 0) == 0) {
                        texturesCache[filePath] = bsaFilename;
                    }
                    else if (filePath.rfind("meshes\\", 0) == 0) {
                        meshesCache[filePath] = bsaFilename;
                    }
                    else {
                        anyCache[filePath] = bsaFilename;
                    }
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

    std::string internalPath = normalizePath(relativePath);

    // If the caller is asking for a texture, only consult the Textures cache.
    if (internalPath.rfind("textures\\", 0) == 0) {
        if (auto it = texturesCache.find(internalPath); it != texturesCache.end()) {
            return it->second;
        }
        return "";
    }
    // If the caller is asking for a mesh, only consult the Meshes cache.
    if (internalPath.rfind("meshes\\", 0) == 0) {
        if (auto it = meshesCache.find(internalPath); it != meshesCache.end()) {
            return it->second;
        }
        return "";
    }
    // Otherwise, fall back to the general cache (non-standard top-level folders).
    if (auto it = anyCache.find(internalPath); it != anyCache.end()) {
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

        // Use the original relativePath (libbsarch expects forward slashes),
        // but normalize here if needed: we can safely pass the original, as
        // bsa.extract_to_memory handles '/'.
        libbsarch::memory_blob blob = bsa.extract_to_memory(relativePath);
        const char* data = static_cast<const char*>(blob.data);
        return std::vector<char>(data, data + blob.size);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to extract " << relativePath << " from " << bsaName << ": " << e.what() << std::endl;
        return {};
    }
}


std::string BsaManager::normalizePath(const std::string& p) {
    std::string s = p;
    std::replace(s.begin(), s.end(), '/', '\\');
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    // Remove accidental leading backslash to make rfind("textures\\",0) work
    if (!s.empty() && (s[0] == '\\')) s.erase(0, 1);
    return s;
}