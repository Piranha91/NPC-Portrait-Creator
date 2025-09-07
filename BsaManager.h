#pragma once

#include <string>
#include <vector>
#include <filesystem>

class BsaManager {
public:
    BsaManager() = default;
    void loadArchives(const std::string& directory);
    bool fileExists(const std::string& relativePath) const;
    size_t getArchiveCount() const;

private:
    std::vector<std::filesystem::path> bsaPaths;
};