#include "BsaManager.h"
#include <iostream>
#include <algorithm>
#include <libbsarch.h>

struct BsaPtrDeleter {
    void operator()(void* ptr) const {
        bsa_free(ptr);
    }
};
using UniqueBsaPtr = std::unique_ptr<void, BsaPtrDeleter>;

void BsaManager::loadArchives(const std::string& directory) {
    bsaPaths.clear();
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
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning directory for archives: " << e.what() << std::endl;
    }
}

bool BsaManager::fileExists(const std::string& relativePath) const {
    if (relativePath.empty()) {
        return false;
    }

    std::string internalPath = relativePath;
    std::replace(internalPath.begin(), internalPath.end(), '/', '\\');

    for (const auto& bsaPath : bsaPaths) {
        UniqueBsaPtr bsaHandle(bsa_create());

        const auto bsaPathW = bsaPath.wstring();
        const auto* bsaPathU16 = reinterpret_cast<const wchar_t*>(bsaPathW.c_str());

        auto result = bsa_load_from_file(bsaHandle.get(), bsaPathU16);
        if (result.code != BSA_RESULT_NONE) {
            continue;
        }

        const std::wstring relPathW = std::filesystem::path(internalPath).wstring();
        const auto* relPathU16 = reinterpret_cast<const wchar_t*>(relPathW.c_str());

        if (bsa_file_exists(bsaHandle.get(), relPathU16)) {
            return true;
        }
    }
    return false;
}

size_t BsaManager::getArchiveCount() const {
    return bsaPaths.size();
}