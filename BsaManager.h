#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

class BsaManager {
public:
    BsaManager() = default;
    void loadArchives(const std::string& directory, const std::filesystem::path& cache_dir);
    std::string findFileInArchives(const std::string& relativePath) const;
    std::vector<char> extractFile(const std::string& relativePath) const;
    size_t getArchiveCount() const;

private:
    // New helper methods for loading and saving the JSON cache
    bool loadCache(const std::string& bsa_directory);
    void saveCache();
    // New helper for the global search fallback
    std::vector<char> findAndExtractDirectly(const std::string& internalPath, const std::filesystem::path& bsaToExclude) const;

    std::filesystem::path cacheFilePath; // Path to bsa_contents_cache.json
    std::vector<std::filesystem::path> bsaPaths;
    // Separate caches so textures-only queries don't scan meshes archives and vice versa.
    std::unordered_map<std::string, std::string> anyCache;      // fallback / non-standard paths
    std::unordered_map<std::string, std::string> texturesCache; // keys start with "textures\"
    std::unordered_map<std::string, std::string> meshesCache;   // keys start with "meshes\"

    static std::string normalizePath(const std::string& p);
};