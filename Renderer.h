#ifndef RENDERER_H
#define RENDERER_H

#include <string>
#include <memory>
#include "Shader.h"
#include "Camera.h"
#include "NifModel.h"
#include "TextureManager.h"
#include "BsaManager.h"
#include "Skeleton.h"
#include <chrono> 

struct GLFWwindow;
enum class SkeletonType { None, Female, Male, FemaleBeast, MaleBeast, Custom };
class Renderer {
public:
    Renderer(int width, int height, const std::string& app_dir);
    ~Renderer();
    // --- Core Methods ---
    void init(bool headless);
    void run();
    void renderFrame();
    void saveToPNG(const std::string& path);
    // --- NIF and Camera Control ---
    void loadNifModel(const std::string& path);
    void loadCustomSkeleton(const std::string& path);
    void detectAndSetSkeleton(const nifly::NifFile& nif);
    void setCamera(float posX, float posY, float posZ, float pitch, float yaw);
    void setFallbackRootDirectory(const std::string& path);

    // --- NEW: Public setters for mugshot offsets ---
    void setMugshotTopOffset(float offset) { headTopOffset = offset; }
    void setMugshotBottomOffset(float offset) { headBottomOffset = offset; }

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

    std::string configPath;

    GLFWwindow* window = nullptr;
    Shader shader;
    std::unique_ptr<NifModel> model;

    int screenWidth, screenHeight;
    std::string currentNifPath;

    // Directory Management
    std::string rootDirectory;
    std::string fallbackRootDirectory;
    TextureManager textureManager;
    BsaManager bsaManager;
    std::string appDirectory;

    // Skeletons loaded from game data
    Skeleton femaleSkeleton;
    Skeleton maleSkeleton;
    Skeleton femaleBeastSkeleton;
    Skeleton maleBeastSkeleton;
    // Skeleton loaded by the user at runtime
    Skeleton customSkeleton;

    Skeleton* activeSkeleton = nullptr;
    SkeletonType currentSkeletonType = SkeletonType::None;

    // --- NEW: Mugshot framing offsets ---
    float headTopOffset = 0.20f;    // Default: 20% margin at the top
    float headBottomOffset = -0.05f; // Default: -5% margin (overshoot) at the bottom

    // --- Add these for high-level load profiling ---
    std::chrono::high_resolution_clock::time_point nifLoadStartTime;
    bool newModelLoaded = false;
};

#endif // RENDERER_H