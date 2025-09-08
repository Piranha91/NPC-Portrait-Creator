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
    void run();
    void renderFrame();
    void saveToPNG(const std::string& path);

    // --- NIF and Camera Control ---
    void loadNifModel(const std::string& path);
    void setCamera(float posX, float posY, float posZ, float pitch, float yaw);
    void setFallbackRootDirectory(const std::string& path);

    // --- Public Input Handlers ---
    void HandleMouseButton(int button, int action, int mods);
    void HandleCursorPosition(double xpos, double ypos);
    void HandleScroll(double xoffset, double yoffset);
    void HandleKey(int key, int scancode, int action, int mods);
    void HandleFramebufferSize(int width, int height);

    // --- Public members for callbacks ---
    // These are public so the global callback functions can access them.
    Camera camera;
    float lastX, lastY;
    bool firstMouse = true;
    bool isPanning = false;
    bool isRotating = false;

private:
    // --- UI Methods ---
    void initUI();
    void renderUI();
    void shutdownUI();

    // --- Config Methods ---
    void loadConfig();
    void saveConfig();

    std::string configPath = "mugshotter_config.txt";

    GLFWwindow* window = nullptr;
    Shader shader;
    std::unique_ptr<NifModel> model;

    int screenWidth, screenHeight;
    std::string currentNifPath;

    // Directory Management
    std::string rootDirectory;
    std::string fallbackRootDirectory;
    TextureManager textureManager;
};

#endif // RENDERER_H