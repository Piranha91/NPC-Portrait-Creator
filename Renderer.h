#ifndef RENDERER_H
#define RENDERER_H

#include <string>
#include <memory>
#include "Shader.h"
#include "Camera.h"
#include "NifModel.h"
#include "AssetManager.h"
#include "TextureManager.h"
#include "BsaManager.h"
#include "Skeleton.h"
#include "Version.h"
#include <chrono> 
#include <nlohmann/json.hpp>

struct GLFWwindow;
enum class SkeletonType { None, Female, Male, FemaleBeast, MaleBeast, Custom };
struct Light {
    int type = 0; // 0=disabled, 1=ambient, 2=directional
    glm::vec3 direction{ 0.0f };
    glm::vec3 color{ 1.0f };
    float intensity = 1.0f;
};

class Renderer {

public:
    Renderer(int width, int height, const std::string& app_dir);
    ~Renderer();
    // --- Core Methods ---
    void init(bool headless);
    void run();
    void renderFrame();
    void saveToPNG(const std::string& path);
    GLFWwindow* getWindow() const { return window; }
    void processDirectory();

	// --- Configuration Management ---
    void loadConfig();
    void saveConfig();

    // --- NIF and Camera Control ---
    void loadNifModel(const std::string& path);
    void loadCustomSkeleton(const std::string& path);
    void detectAndSetSkeleton(const nifly::NifFile& nif);
    void setGameDataDirectory(const std::string& path) { gameDataDirectory = path; }
    void setDataFolders(const std::vector<std::string>& folders);
    std::vector<std::string>& getDataFolders() { return dataFolders; }

    // --- Public Setters for Configurable Options ---
    void setBackgroundColor(const glm::vec3& color) { backgroundColor = color; }
    void setMugshotTopOffset(float offset) { headTopOffset = offset; }
    void setMugshotBottomOffset(float offset) { headBottomOffset = offset; }
    void setImageResolutionX(int width) { imageXRes = width; }
    void setImageResolutionY(int height) { imageYRes = height; }
    void setAbsoluteCamera(float x, float y, float z, float p, float yw) {
        camX = x; camY = y; camZ = z; camPitch = p; camYaw = yw;
    }
    void setLightingProfile(const std::string& path) { lightingProfilePath = path; }

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
    void updateAssetManagerPaths();

    // --- Core Members ---
    GLFWwindow* window = nullptr;
    Shader shader;
    glm::vec3 backgroundColor;
    std::unique_ptr<NifModel> model;
    AssetManager assetManager;
    TextureManager textureManager;
    std::string appDirectory;
    int screenWidth, screenHeight;
    bool isHeadless = false;
    bool uiInitialized = false; // for non-headless mode

    // --- Configuration ---
    std::string configPath;
    std::string currentNifPath;
    std::string gameDataDirectory;
    std::vector<std::string> dataFolders;

    // Skeletons loaded from game data
    Skeleton femaleSkeleton;
    Skeleton maleSkeleton;
    Skeleton femaleBeastSkeleton;
    Skeleton maleBeastSkeleton;
    // Skeleton loaded by the user at runtime
    Skeleton customSkeleton;

    Skeleton* activeSkeleton = nullptr;
    SkeletonType currentSkeletonType = SkeletonType::None;

	// Lighting
    std::string lightingProfilePath;
    std::vector<Light> lights;
    void loadLightingProfile(const std::string& path);

    // Camera settings
    float camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    float camPitch = 0.0f, camYaw = 0.0f;

    // Image output settings
    int imageXRes = 750;
    int imageYRes = 750;

    // --- NEW: Mugshot framing offsets ---
    float headTopOffset = 0.20f;    // Default: 20% margin at the top
    float headBottomOffset = -0.05f; // Default: -5% margin (overshoot) at the bottom

    // NEW: Add this variable to request a screenshot (to enable frame delay and avoid including the UI).
    std::string screenshotPath;

    // --- Add these for high-level load profiling ---
    std::chrono::high_resolution_clock::time_point nifLoadStartTime;
    bool newModelLoaded = false;
};

#endif // RENDERER_H