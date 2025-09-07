#ifndef RENDERER_H
#define RENDERER_H

#include <string>
#include <memory>
#include "Shader.h"
#include "Camera.h"
#include "NifModel.h"
#include "TextureManager.h"

struct GLFWwindow;

class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();

    // --- Core Methods ---
    void init(bool headless);
    void run(); // Main loop for GUI mode
    void renderFrame();
    void saveToPNG(const std::string& path);

    // --- NIF and Camera Control ---
    void loadNifModel(const std::string& path);
    void setCamera(float posX, float posY, float posZ, float pitch, float yaw);

    // --- New/Updated Methods ---
    void setFallbackRootDirectory(const std::string& path);

private:
    // --- UI Methods ---
    void initUI();
    void renderUI();
    void shutdownUI();

    // --- Input and State ---
    void processInput();
    // UPDATED: Unified config methods
    void loadConfig();
    void saveConfig();

    std::string configPath = "mugshotter_config.txt";

    GLFWwindow* window = nullptr;
    Shader shader;
    Camera camera;
    std::unique_ptr<NifModel> model;

    int screenWidth, screenHeight;
    float lastX, lastY;
    bool firstMouse = true;
    std::string currentNifPath;

    // UPDATED: Now an 'active' root directory
    std::string rootDirectory;
    // NEW: Persistent fallback directory
    std::string fallbackRootDirectory;
    TextureManager textureManager;
};

#endif // RENDERER_H