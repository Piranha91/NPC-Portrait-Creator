// TextureManager.cpp
#include "TextureManager.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <gli/gli.hpp>
#include <chrono>

TextureManager::~TextureManager() {
    cleanup();
}

void TextureManager::setActiveDirectories(const std::string& rootDir, const std::string& fallbackDir, const std::string& cacheDir) {
    rootDirectory = rootDir;
    fallbackRootDirectory = fallbackDir;

    if (!rootDirectory.empty()) {
        rootBsaManager.loadArchives(rootDirectory, cacheDir);
    }
    if (!fallbackRootDirectory.empty()) {
        fallbackBsaManager.loadArchives(fallbackRootDirectory, cacheDir);
    }
}

GLuint TextureManager::loadTexture(const std::string& relativePath) {
    if (relativePath.empty()) {
        return 0;
    }

    // 1. Check if texture is already in our GPU cache
    auto it = textureCache.find(relativePath);
    if (it != textureCache.end()) {
        return it->second;
    }

    // START PROFILING ASSET FIND
    auto start_find = std::chrono::high_resolution_clock::now();

    std::vector<char> fileData;
    bool found = false;

    // 2. Search for the file data (loose files first, then BSAs)
    std::filesystem::path loosePath = std::filesystem::path(rootDirectory) / relativePath;
    if (std::filesystem::exists(loosePath)) {
        std::ifstream file(loosePath, std::ios::binary);
        fileData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        found = true;
    }
    else {
        loosePath = std::filesystem::path(fallbackRootDirectory) / relativePath;
        if (std::filesystem::exists(loosePath)) {
            std::ifstream file(loosePath, std::ios::binary);
            fileData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            found = true;
        }
    }

    if (!found) {
        fileData = rootBsaManager.extractFile(relativePath);
        if (fileData.empty()) {
            fileData = fallbackBsaManager.extractFile(relativePath);
        }
    }

    // END PROFILING ASSET FIND
    auto end_find = std::chrono::high_resolution_clock::now();
    auto duration_find = std::chrono::duration_cast<std::chrono::milliseconds>(end_find - start_find);
    std::cout << "    [Profile] Asset Find (" << relativePath << ") took: " << duration_find.count() << " ms\n";

    // 3. If we found data, upload it to the GPU
    if (!fileData.empty()) {
        GLuint textureID = uploadDDSToGPU(fileData);
        if (textureID != 0) {
            textureCache[relativePath] = textureID; // Cache the new texture
            return textureID;
        }
    }

    std::cerr << "Warning: Texture not found or failed to load: " << relativePath << std::endl;
    textureCache[relativePath] = 0; // Cache the failure to avoid repeated attempts
    return 0;
}

GLuint TextureManager::uploadDDSToGPU(const std::vector<char>& data) {
    // START PROFILING ASSET GET/UPLOAD
    auto start_get = std::chrono::high_resolution_clock::now();

    gli::texture tex = gli::load(data.data(), data.size());
    if (tex.empty()) {
        return 0;
    }

    gli::gl gl(gli::gl::PROFILE_GL33);
    gli::gl::format const format = gl.translate(tex.format(), tex.swizzles());
    GLenum target = gl.translate(tex.target());

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(target, textureID);
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(tex.levels() - 1));
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, format.Swizzles[0]);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, format.Swizzles[1]);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, format.Swizzles[2]);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, format.Swizzles[3]);

    glm::tvec3<GLsizei> const extent(tex.extent());
    GLsizei const faceTotal = static_cast<GLsizei>(tex.layers() * tex.faces());

    switch (tex.target()) {
    case gli::TARGET_1D:
        glTexStorage1D(target, static_cast<GLint>(tex.levels()), format.Internal, extent.x);
        break;
    case gli::TARGET_1D_ARRAY:
    case gli::TARGET_2D:
    case gli::TARGET_CUBE:
        glTexStorage2D(target, static_cast<GLint>(tex.levels()), format.Internal, extent.x, extent.y);
        break;
    case gli::TARGET_2D_ARRAY:
    case gli::TARGET_3D:
    case gli::TARGET_CUBE_ARRAY:
        glTexStorage3D(target, static_cast<GLint>(tex.levels()), format.Internal, extent.x, extent.y, extent.z);
        break;
    default:
        return 0;
    }

    for (std::size_t layer = 0; layer < tex.layers(); ++layer) {
        for (std::size_t face = 0; face < tex.faces(); ++face) {
            for (std::size_t level = 0; level < tex.levels(); ++level) {
                GLsizei const layerGL = static_cast<GLsizei>(layer);
                glm::tvec3<GLsizei> extent(tex.extent(level));
                target = gli::is_target_cube(tex.target())
                    ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
                    : target;

                if (gli::is_compressed(tex.format())) {
                    glCompressedTexSubImage2D(target, static_cast<GLint>(level), 0, 0, extent.x, extent.y,
                        format.Internal, static_cast<GLsizei>(tex.size(level)), tex.data(layer, face, level));
                }
                else {
                    glTexSubImage2D(target, static_cast<GLint>(level), 0, 0, extent.x, extent.y,
                        format.External, format.Type, tex.data(layer, face, level));
                }
            }
        }
    }

    // If it has mipmaps, generate them
    if (tex.levels() > 1) {
        glGenerateMipmap(target);
    }

    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    auto end_get = std::chrono::high_resolution_clock::now();
    auto duration_get = std::chrono::duration_cast<std::chrono::milliseconds>(end_get - start_get);
    std::cout << "    [Profile] Asset Get/Upload took: " << duration_get.count() << " ms\n";

    return textureID;
}

void TextureManager::cleanup() {
    for (auto const& [path, id] : textureCache) {
        if (id != 0) {
            glDeleteTextures(1, &id);
        }
    }
    textureCache.clear();
}