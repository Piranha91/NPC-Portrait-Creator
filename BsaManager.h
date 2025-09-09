#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <vector>

class BsaManager {
public:
    BsaManager() = default;
    void loadArchives(const std::string& directory);
    std::string findFileInArchives(const std::string& relativePath) const;
    std::vector<char> extractFile(const std::string& relativePath) const;
    size_t getArchiveCount() const;

private:
    std::vector<std::filesystem::path> bsaPaths;
    // Separate caches so textures-only queries don't scan meshes archives and vice versa.
    std::unordered_map<std::string, std::string> anyCache;      // fallback / non-standard paths
    std::unordered_map<std::string, std::string> texturesCache; // keys start with "textures\"
    std::unordered_map<std::string, std::string> meshesCache;   // keys start with "meshes\"

    static std::string normalizePath(const std::string& p);
};