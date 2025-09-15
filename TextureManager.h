#pragma once

#include <string>
#include <unordered_map>
#include <glad/glad.h>

// Forward-declare AssetManager to avoid a circular include dependency.
class AssetManager;

class TextureManager {
public:
    // MODIFICATION: Constructor now takes a reference to the AssetManager.
    explicit TextureManager(AssetManager& manager);
    ~TextureManager();

    GLuint loadTexture(const std::string& relativePath);

private:
    void cleanup();
    GLuint uploadDDSToGPU(const std::vector<char>& data);

    // MODIFICATION: Holds a reference to the main AssetManager.
    AssetManager& assetManager;

    // This cache is for GPU texture IDs, which is still this class's responsibility.
    std::unordered_map<std::string, GLuint> textureCache;
};