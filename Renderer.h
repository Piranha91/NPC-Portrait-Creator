#ifndef RENDERER_H
#define RENDERER_H

#include <string>
#include <memory>
#include "Shader.h"
#include "Camera.h"
#include "NifModel.h"

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

private:
    // --- UI Methods ---
    void initUI();
    void renderUI();
    void shutdownUI();

    // --- Input and State ---
    void processInput();
    void loadLastNifPath();
    void saveLastNifPath(const std::string& path);
    std::string configPath = "mugshotter_config.txt";

    GLFWwindow* window = nullptr;
    Shader shader;
    Camera camera;
    std::unique_ptr<NifModel> model;
    
    int screenWidth, screenHeight;
    float lastX, lastY;
    bool firstMouse = true;
    std::string currentNifPath;
};

#endif // RENDERER_H

