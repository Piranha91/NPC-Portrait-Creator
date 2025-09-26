#pragma once

#include <string>
#include <unordered_map>
#include <glad/glad.h>

// Forward-declare AssetManager to avoid a circular include dependency.
class AssetManager;

// A struct to hold both the texture's GPU ID and its OpenGL target type.
struct TextureInfo {
    GLuint id = 0;
    GLenum target = GL_TEXTURE_2D; // Default to 2D texture
};

class TextureManager {
public:
    // MODIFICATION: Constructor now takes a reference to the AssetManager.
    explicit TextureManager(AssetManager& manager);
    ~TextureManager();

    // MODIFICATION: Change the return type from GLuint to the new TextureInfo struct.
    TextureInfo loadTexture(const std::string& relativePath);

    void cleanup();

private:
    TextureInfo uploadDDSToGPU(const std::vector<char>& data);

    // MODIFICATION: Holds a reference to the main AssetManager.
    AssetManager& assetManager;

    // This cache is for GPU texture IDs, which is still this class's responsibility.
    std::unordered_map<std::string, TextureInfo> textureCache;
};