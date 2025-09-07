#include "TextureManager.h"
#include <iostream>
#include <filesystem>

void TextureManager::setActiveDirectories(const std::string& rootDir, const std::string& fallbackDir) {
    rootDirectory = rootDir;
    fallbackRootDirectory = fallbackDir;

    std::cout << "Scanning for archives..." << std::endl;
    rootBsaManager.loadArchives(rootDirectory);
    std::cout << "  Found " << rootBsaManager.getArchiveCount() << " archives in root directory." << std::endl;

    fallbackBsaManager.loadArchives(fallbackRootDirectory);
    std::cout << "  Found " << fallbackBsaManager.getArchiveCount() << " archives in fallback directory." << std::endl;
}

void TextureManager::checkTexture(const std::string& relativePath) const {
    std::filesystem::path primaryLoosePath = std::filesystem::path(rootDirectory) / relativePath;

    // 1. Check for loose file in root directory
    if (std::filesystem::exists(primaryLoosePath)) {
        std::cout << primaryLoosePath.string() << " [FOUND (Loose File)]" << std::endl;
        return;
    }

    // 2. Check for loose file in fallback directory
    std::filesystem::path fallbackLoosePath = std::filesystem::path(fallbackRootDirectory) / relativePath;
    if (std::filesystem::exists(fallbackLoosePath)) {
        std::cout << primaryLoosePath.string() << " [NOT FOUND]" << std::endl;
        std::cout << "  -> " << fallbackLoosePath.string() << " [FOUND (Loose File)]" << std::endl;
        return;
    }

    // 3. Check for file in root directory's BSAs
    std::string foundInBsa = rootBsaManager.findFileInArchives(relativePath);
    if (!foundInBsa.empty()) {
        std::cout << primaryLoosePath.string() << " [NOT FOUND]" << std::endl;
        std::cout << "  -> " << fallbackLoosePath.string() << " [NOT FOUND]" << std::endl;
        std::cout << "  -> " << primaryLoosePath.string() << " [FOUND (in " << foundInBsa << ")]" << std::endl;
        return;
    }

    // 4. Check for file in fallback directory's BSAs
    foundInBsa = fallbackBsaManager.findFileInArchives(relativePath);
    if (!foundInBsa.empty()) {
        std::cout << primaryLoosePath.string() << " [NOT FOUND]" << std::endl;
        std::cout << "  -> " << fallbackLoosePath.string() << " [NOT FOUND]" << std::endl;
        std::cout << "  -> " << fallbackLoosePath.string() << " [FOUND (in " << foundInBsa << ")]" << std::endl;
        return;
    }

    // If not found anywhere
    std::cout << primaryLoosePath.string() << " [NOT FOUND]" << std::endl;
    std::cout << "  -> " << fallbackLoosePath.string() << " [NOT FOUND (incl. BSAs)]" << std::endl;
}