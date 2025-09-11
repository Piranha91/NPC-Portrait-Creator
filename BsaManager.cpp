#include "BsaManager.h"
#include <iostream>
#include <algorithm>
#include <fstream>          // For std::ifstream and std::ofstream
#include <iomanip>          // For std::setw
#include <unordered_set>    // For std::unordered_set
#include <stdexcept> 
#include <functional>   // Add this for std::hash

// Include the correct C++ wrapper header for reading archives
#include <bs_archive.h>

std::string sanitizePathForFilename(const std::string& path) {
    std::string sanitized = path;
    // Replace characters that are invalid in filenames
    std::replace(sanitized.begin(), sanitized.end(), ':', '_');
    std::replace(sanitized.begin(), sanitized.end(), '\\', '_');
    std::replace(sanitized.begin(), sanitized.end(), '/', '_');

    // Optional: clean up repeated underscores for readability
    auto new_end = std::unique(sanitized.begin(), sanitized.end(),
        [](char a, char b) { return a == '_' && b == '_'; });
    sanitized.erase(new_end, sanitized.end());

    return sanitized;
}

void BsaManager::saveCache() {
    std::cout << "--- Saving BSA contents to cache: " << cacheFilePath.string() << " ---" << std::endl;
    try {
        // --- FIX: Group files by their source BSA for a more organized cache file ---
        nlohmann::json archivesJson;
        std::map<std::string, std::vector<std::string>> groupedFiles;

        // Combine all flat maps into one for easier processing
        std::unordered_map<std::string, std::string> combined = anyCache;
        combined.insert(texturesCache.begin(), texturesCache.end());
        combined.insert(meshesCache.begin(), meshesCache.end());

        // Group file paths by their BSA name
        for (const auto& [filePath, bsaName] : combined) {
            groupedFiles[bsaName].push_back(filePath);
        }
        archivesJson = groupedFiles;

        nlohmann::json metadata;
        std::vector<std::string> sourceBsaNames;
        for (const auto& p : bsaPaths) {
            sourceBsaNames.push_back(p.filename().string());
        }
        std::sort(sourceBsaNames.begin(), sourceBsaNames.end());
        metadata["sources"] = sourceBsaNames;

        nlohmann::json finalCache;
        finalCache["__metadata"] = metadata;
        finalCache["archives"] = archivesJson;

        std::ofstream o(cacheFilePath);
        o << std::setw(4) << finalCache << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "Failed to save BSA cache: " << e.what() << std::endl;
    }
}

bool BsaManager::loadCache(const std::string& bsa_directory) {
    if (!std::filesystem::exists(cacheFilePath)) {
        return false;
    }

    try {
        // 1. Get the current list of BSA files on disk for validation.
        std::vector<std::string> diskBsaNames;
        for (const auto& entry : std::filesystem::directory_iterator(bsa_directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".bsa") {
                diskBsaNames.push_back(entry.path().filename().string());
            }
        }
        std::sort(diskBsaNames.begin(), diskBsaNames.end());

        // 2. Load the cache and get the list of BSAs it was built from.
        std::ifstream f(cacheFilePath);
        nlohmann::json data = nlohmann::json::parse(f);

        if (!data.contains("__metadata") || !data["__metadata"].contains("sources") || !data.contains("archives")) {
            std::cerr << "Cache is invalid (missing metadata or archives section). Rebuilding." << std::endl;
            return false;
        }

        std::vector<std::string> cachedBsaNames = data["__metadata"]["sources"].get<std::vector<std::string>>();
        std::sort(cachedBsaNames.begin(), cachedBsaNames.end());

        // 3. Compare the lists. If they don't match, the cache is stale.
        if (diskBsaNames != cachedBsaNames) {
            std::cout << "--- BSA cache is stale (archive list has changed). Rebuilding. ---" << std::endl;
            return false;
        }

        // --- Cache is valid, proceed with loading ---
        std::cout << "--- Loading BSA contents from valid cache: " << cacheFilePath.string() << " ---" << std::endl;

        anyCache.clear();
        texturesCache.clear();
        meshesCache.clear();
        bsaPaths.clear();

        // --- FIX: Parse the grouped archive structure and repopulate the flat in-memory maps ---
        const auto& archivesData = data["archives"];
        for (const auto& [bsaName, fileList] : archivesData.items()) {
            for (const auto& filePathJson : fileList) {
                std::string filePath = filePathJson.get<std::string>();
                // Route to the correct in-memory cache for fast lookups
                if (filePath.rfind("textures\\", 0) == 0) {
                    texturesCache[filePath] = bsaName;
                }
                else if (filePath.rfind("meshes\\", 0) == 0) {
                    meshesCache[filePath] = bsaName;
                }
                else {
                    anyCache[filePath] = bsaName;
                }
            }
        }

        // Populate bsaPaths from the validated diskBsaNames list
        for (const auto& bsaName : diskBsaNames) {
            bsaPaths.push_back(std::filesystem::path(bsa_directory) / bsaName);
        }

        std::cout << "--- BSA Cache Loaded Successfully ---" << std::endl;
        return true;

    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load or parse BSA cache: " << e.what() << ". Rebuilding." << std::endl;
        return false;
    }
}


void BsaManager::loadArchives(const std::string& directory, const std::filesystem::path& cache_dir) {
    if (directory.empty() || !std::filesystem::exists(directory)) {
        return;
    }

    const std::filesystem::path cacheSubfolder = cache_dir / "BSA Content Caches";
    try {
        std::filesystem::create_directories(cacheSubfolder);
    }
    catch (const std::exception& e) {
        std::cerr << "Error creating cache directory: " << e.what() << std::endl;
        return;
    }

    std::string sanitizedDir = sanitizePathForFilename(directory);
    std::string cacheFilename = sanitizedDir + ".json";
    cacheFilePath = cacheSubfolder / cacheFilename;

    if (loadCache(directory)) {
        return;
    }

    bsaPaths.clear();
    anyCache.clear();
    texturesCache.clear();
    meshesCache.clear();

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

                    // --- LOGGING RESTORED AND ENABLED ---
                    bool showDebug = true;
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

        saveCache();
        std::cout << "--- BSA Caching Complete ---" << std::endl;
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning directory for archives: " << e.what() << std::endl;
    }
}


// New helper to search all loaded BSAs directly, used as a fallback.
std::vector<char> BsaManager::findAndExtractDirectly(const std::string& internalPath, const std::filesystem::path& bsaToExclude) const {
    std::string extractionPath = internalPath;
    std::replace(extractionPath.begin(), extractionPath.end(), '\\', '/');

    for (const auto& bsaPath : bsaPaths) {
        if (!bsaToExclude.empty() && bsaPath == bsaToExclude) {
            continue; // Skip the BSA that we already know failed
        }
        try {
            libbsarch::bs_archive bsa;
            bsa.load_from_disk(bsaPath.wstring());
            // The check for existence is simply trying to extract it.
            libbsarch::memory_blob blob = bsa.extract_to_memory(extractionPath);
            const char* data = static_cast<const char*>(blob.data);
            std::cout << "Fallback success: Found '" << internalPath << "' in '" << bsaPath.filename().string() << "'" << std::endl;
            // A full cache rebuild upon next run will fix this permanently.
            return std::vector<char>(data, data + blob.size);
        }
        catch (const std::runtime_error&) { // This is the correct type
            // Expected when a file isn't in this BSA, so we silently continue.
            continue;
        }
        catch (const std::exception&) {
            // Ignore other errors during fallback search.
        }
    }
    return {}; // Not found in any BSA.
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
    if (relativePath.empty()) {
        return {};
    }

    std::string internalPath = normalizePath(relativePath);
    std::string bsaName = findFileInArchives(internalPath);

    // Case 1: Cache Miss - The file is not in our maps.
    if (bsaName.empty()) {
        // Perform a global search across all BSAs since we have no cached location.
        return findAndExtractDirectly(internalPath, "");
    }

    // Case 2: Cache Hit - We have a predicted location for the file.
    std::filesystem::path bsaFullPath;
    for (const auto& p : bsaPaths) {
        if (p.filename().string() == bsaName) {
            bsaFullPath = p;
            break;
        }
    }

    if (bsaFullPath.empty()) {
        // The cached BSA doesn't exist on disk. Fall back to a global search.
        std::cout << "Cached BSA '" << bsaName << "' not found on disk. Falling back to global search for: " << internalPath << std::endl;
        return findAndExtractDirectly(internalPath, "");
    }

    try {
        libbsarch::bs_archive bsa;
        bsa.load_from_disk(bsaFullPath.wstring());
        std::string extractionPath = internalPath;
        std::replace(extractionPath.begin(), extractionPath.end(), '\\', '/');

        libbsarch::memory_blob blob = bsa.extract_to_memory(extractionPath);
        const char* data = static_cast<const char*>(blob.data);
        return std::vector<char>(data, data + blob.size);
    }
    catch (const std::exception& e) {
        // The file wasn't in the cached BSA. Fall back to a global search.
        std::cerr << "Failed to extract " << internalPath << " from cached BSA " << bsaName << ": " << e.what() << std::endl;
        std::cerr << "Cache might be stale. Falling back to global BSA search." << std::endl;
        return findAndExtractDirectly(internalPath, bsaFullPath);
    }
}


std::string BsaManager::normalizePath(const std::string& p) {
    std::string s = p;
    std::replace(s.begin(), s.end(), '/', '\\');
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (!s.empty() && (s[0] == '\\')) s.erase(0, 1);

    // If the path already has a top-level directory, don't modify it further.
    if (s.rfind("textures\\", 0) == 0 || s.rfind("meshes\\", 0) == 0) {
        return s;
    }

    // Find the file extension to decide on the prefix.
    size_t dot_pos = s.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string ext = s.substr(dot_pos);
        if (ext == ".dds") {
            s = "textures\\" + s;
        }
        else if (ext == ".nif" || ext == ".tri") {
            s = "meshes\\" + s;
        }
    }

    return s;
}