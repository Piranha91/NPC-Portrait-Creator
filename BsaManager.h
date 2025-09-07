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
    std::unordered_map<std::string, std::string> fileCache;
};