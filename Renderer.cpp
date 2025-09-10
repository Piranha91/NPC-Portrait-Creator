#define GLM_ENABLE_EXPERIMENTAL

#include "Renderer.h"
#include "BsaManager.h"
#include "Skeleton.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <vector>
#include <algorithm> 
#include <filesystem>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp> // For logging glm::vec3

// --- ImGui Includes ---
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// --- File Dialog Includes ---
#include "tinyfiledialogs.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// --- Global Callback Prototypes ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

Renderer::Renderer(int width, int height)
    : screenWidth(width), screenHeight(height),
    camera(glm::vec3(0.0f, 50.0f, 0.0f)), // Initialize with a target
    lastX(width / 2.0f), lastY(height / 2.0f),
    fallbackRootDirectory("C:\\Games\\Steam\\steamapps\\common\\Skyrim Special Edition\\Data") {
}


Renderer::~Renderer() {
    shutdownUI();
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void Renderer::init(bool headless) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    if (headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    window = glfwCreateWindow(screenWidth, screenHeight, "Mugshotter", NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwMakeContextCurrent(window);

    // --- Register Callbacks ---
    glfwSetWindowUserPointer(window, this);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, screenWidth, screenHeight);
    shader.load("shaders/basic.vert", "shaders/basic.frag");

    // --- BSA and Skeleton Loading ---
    // Load config first to get the fallback directory
    loadConfig();

    // Use the fallback directory to load archives
    bsaManager.loadArchives(fallbackRootDirectory);

    // --- Load all standard and beast skeletons from BSAs ---
    std::cout << "[Skeleton Load] Attempting to load all default skeletons from BSAs...\n";

    const std::string femaleSkelPath = "meshes\\actors\\character\\character assets female\\skeleton_female.nif";
    auto femaleData = bsaManager.extractFile(femaleSkelPath);
    if (!femaleData.empty()) femaleSkeleton.loadFromMemory(femaleData, "skeleton_female.nif");

    const std::string maleSkelPath = "meshes\\actors\\character\\character assets\\skeleton.nif";
    auto maleData = bsaManager.extractFile(maleSkelPath);
    if (!maleData.empty()) maleSkeleton.loadFromMemory(maleData, "skeleton.nif");

    const std::string femaleBeastSkelPath = "meshes\\actors\\character\\character assets female\\skeletonbeast_female.nif";
    auto femaleBeastData = bsaManager.extractFile(femaleBeastSkelPath);
    if (!femaleBeastData.empty()) femaleBeastSkeleton.loadFromMemory(femaleBeastData, "skeletonbeast_female.nif");

    const std::string maleBeastSkelPath = "meshes\\actors\\character\\character assets\\skeletonbeast.nif";
    auto maleBeastData = bsaManager.extractFile(maleBeastSkelPath);
    if (!maleBeastData.empty()) maleBeastSkeleton.loadFromMemory(maleBeastData, "skeletonbeast.nif");

    // No default skeleton is set at startup; it will be detected when a NIF is loaded.
    activeSkeleton = nullptr;
    currentSkeletonType = SkeletonType::None;

    model = std::make_unique<NifModel>();

    if (!headless) {
        initUI();
        if (!currentNifPath.empty()) {
            std::ifstream testFile(currentNifPath);
            if (testFile.good()) {
                loadNifModel(currentNifPath);
            }
            else {
                std::cout << "Last used NIF not found: " << currentNifPath << std::endl;
            }
        }
    }
}

// --- UI Methods (unchanged) ---
void Renderer::initUI() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.FontGlobalScale = 2.0f; // Scale up for high-DPI displays
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Renderer::run() {
    // --- START: ADD THIS DEBUG CODE ---
    std::cout << "DEBUG: Entering Renderer::run()." << std::endl;
    if (glfwWindowShouldClose(window)) {
        std::cout << "DEBUG: ERROR - Window is already set to close BEFORE the loop starts!" << std::endl;
    }
    else {
        std::cout << "DEBUG: Window is OK. Starting main loop." << std::endl;
    }
    // --- END: ADD THIS DEBUG CODE ---

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI();
        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        renderFrame();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    std::cout << "DEBUG: Exited main loop in Renderer::run()." << std::endl;
}

void Renderer::renderUI() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open NIF...")) {
                const char* filterPatterns[1] = { "*.nif" };
                const char* filePath = tinyfd_openFileDialog("Open NIF File", "", 1, filterPatterns, "NIF Files", 0);
                if (filePath) {
                    loadNifModel(filePath);
                }
            }
            if (ImGui::MenuItem("Set Fallback Data Directory...")) {
                const char* folderPath = tinyfd_selectFolderDialog("Select Fallback Data Folder", fallbackRootDirectory.c_str());
                if (folderPath) {
                    setFallbackRootDirectory(folderPath);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(window, true);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Skeleton")) {
            if (ImGui::MenuItem("Load Custom Skeleton...")) {
                const char* filterPatterns[1] = { "*.nif" };
                const char* skelPath = tinyfd_openFileDialog("Open Skeleton NIF", "", 1, filterPatterns, "NIF Files", 0);
                if (skelPath && skelPath[0]) {
                    loadCustomSkeleton(skelPath);
                }
            }
            ImGui::Separator();

            bool sel = (currentSkeletonType == SkeletonType::None);
            if (ImGui::MenuItem("None", nullptr, sel)) {
                currentSkeletonType = SkeletonType::None;
                activeSkeleton = nullptr;
            }

            sel = (currentSkeletonType == SkeletonType::Female);
            if (ImGui::MenuItem("Female", nullptr, sel, femaleSkeleton.isLoaded())) {
                currentSkeletonType = SkeletonType::Female;
                activeSkeleton = &femaleSkeleton;
            }
            sel = (currentSkeletonType == SkeletonType::FemaleBeast);
            if (ImGui::MenuItem("Female Beast", nullptr, sel, femaleBeastSkeleton.isLoaded())) {
                currentSkeletonType = SkeletonType::FemaleBeast;
                activeSkeleton = &femaleBeastSkeleton;
            }

            sel = (currentSkeletonType == SkeletonType::Male);
            if (ImGui::MenuItem("Male", nullptr, sel, maleSkeleton.isLoaded())) {
                currentSkeletonType = SkeletonType::Male;
                activeSkeleton = &maleSkeleton;
            }
            sel = (currentSkeletonType == SkeletonType::MaleBeast);
            if (ImGui::MenuItem("Male Beast", nullptr, sel, maleBeastSkeleton.isLoaded())) {
                currentSkeletonType = SkeletonType::MaleBeast;
                activeSkeleton = &maleBeastSkeleton;
            }

            sel = (currentSkeletonType == SkeletonType::Custom);
            if (ImGui::MenuItem("Custom", nullptr, sel, customSkeleton.isLoaded())) {
                if (customSkeleton.isLoaded()) {
                    currentSkeletonType = SkeletonType::Custom;
                    activeSkeleton = &customSkeleton;
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void Renderer::shutdownUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Renderer::renderFrame() {
    // FIX: Add this check to prevent rendering when minimized.
    if (screenWidth == 0 || screenHeight == 0) {
        return;
    }

    // Convert by dividing each color component by 255.0
    glClearColor(0.227f, 0.239f, 0.251f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader.use();
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)screenWidth / (float)screenHeight, 10.0f, 10000.0f);
    glm::mat4 view = camera.GetViewMatrix();

    // Add this matrix
    glm::mat4 conversionMatrix = glm::mat4(
        -1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    // Apply the conversion
    view = view * conversionMatrix;

    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    if (model) {
        model->draw(shader, camera.Position);
    }
}

void Renderer::HandleFramebufferSize(int width, int height) {
    // Update the screen dimensions stored in the class
    screenWidth = width;
    screenHeight = height;
    // Update the OpenGL viewport to match the new window size
    glViewport(0, 0, width, height);
}

// --- Model Loading and Util ---

void Renderer::detectAndSetSkeleton(const nifly::NifFile& nif) {
    bool hasFemale = false;
    bool hasMale = false;
    bool isBeast = false;

    auto checkString = [&](const std::string& s) {
        std::string lower_s = s;
        std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), ::tolower);
        if (lower_s.find("female") != std::string::npos) hasFemale = true;
        if (lower_s.find("male") != std::string::npos) hasMale = true;
        if (lower_s.find("argonian") != std::string::npos || lower_s.find("khajiit") != std::string::npos) isBeast = true;
        };

    const auto& shapes = nif.GetShapes();

    // 1. Check shape names
    for (const auto* shape : shapes) {
        if (auto* triShape = dynamic_cast<const nifly::BSTriShape*>(shape)) {
            checkString(triShape->name.get());
        }
    }

    // 2. If no gender found, check texture paths
    if (!hasFemale && !hasMale) {
        for (const auto* shape : shapes) {
            const nifly::NiShader* shader = nif.GetShader(const_cast<nifly::NiShape*>(shape));
            if (shader && shader->HasTextureSet()) {
                if (auto* textureSet = nif.GetHeader().GetBlock<nifly::BSShaderTextureSet>(shader->TextureSetRef())) {
                    for (const auto& tex : textureSet->textures) {
                        checkString(tex.get());
                    }
                }
            }
        }
    }

    // 3. Set skeleton based on findings
    if (hasFemale) {
        if (isBeast && femaleBeastSkeleton.isLoaded()) {
            activeSkeleton = &femaleBeastSkeleton;
            currentSkeletonType = SkeletonType::FemaleBeast;
            std::cout << "[Skeleton Detect] Female Beast skeleton auto-selected." << std::endl;
        }
        else if (femaleSkeleton.isLoaded()) {
            activeSkeleton = &femaleSkeleton;
            currentSkeletonType = SkeletonType::Female;
            std::cout << "[Skeleton Detect] Female skeleton auto-selected." << std::endl;
        }
    }
    else if (hasMale) {
        if (isBeast && maleBeastSkeleton.isLoaded()) {
            activeSkeleton = &maleBeastSkeleton;
            currentSkeletonType = SkeletonType::MaleBeast;
            std::cout << "[Skeleton Detect] Male Beast skeleton auto-selected." << std::endl;
        }
        else if (maleSkeleton.isLoaded()) {
            activeSkeleton = &maleSkeleton;
            currentSkeletonType = SkeletonType::Male;
            std::cout << "[Skeleton Detect] Male skeleton auto-selected." << std::endl;
        }
    }
    else {
        activeSkeleton = nullptr;
        currentSkeletonType = SkeletonType::None;
        std::cout << "[Skeleton Detect] No specific skeleton detected. Set to None." << std::endl;
    }
}

void Renderer::loadNifModel(const std::string& path) {
    std::string pathLower = path;
    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

    size_t meshesPos = pathLower.rfind("\\meshes\\");
    if (meshesPos == std::string::npos) {
        meshesPos = pathLower.rfind("/meshes/");
    }

    if (meshesPos != std::string::npos) {
        rootDirectory = path.substr(0, meshesPos);
    }
    else {
        rootDirectory = fallbackRootDirectory;
    }
    textureManager.setActiveDirectories(rootDirectory, fallbackRootDirectory);

    // --- NEW: Perform skeleton detection BEFORE loading the model ---
    nifly::NifFile tempNif;
    if (tempNif.Load(path) == 0) {
        detectAndSetSkeleton(tempNif);
    }
    else {
        std::cerr << "Could not pre-load NIF for skeleton detection." << std::endl;
        activeSkeleton = nullptr; // Fallback
        currentSkeletonType = SkeletonType::None;
    }

    if (!model) {
        model = std::make_unique<NifModel>();
    }

    if (model->load(path, textureManager, activeSkeleton)) {
        currentNifPath = path;
        saveConfig();

        std::cout << "\n--- Calculating Mugshot Camera Position ---\n";
        glm::vec3 modelMinBounds_Zup = model->getHeadMinBounds();
        glm::vec3 modelMaxBounds_Zup = model->getHeadMaxBounds();
        glm::vec3 eyeCenter_Zup = model->hasEyeCenter() ? model->getEyeCenter() : model->getCenter();
        const float bottomCropPercentage = 0.15f;
        const float fovYRadians = glm::radians(45.0f);
        glm::vec3 eyeCenter_Yup = glm::vec3(-eyeCenter_Zup.x, eyeCenter_Zup.z, eyeCenter_Zup.y);
        float modelTop_Yup = modelMaxBounds_Zup.z;
        float modelBottom_Yup = modelMinBounds_Zup.z;
        float totalHeight = modelTop_Yup - modelBottom_Yup;
        float croppedBottom_Yup = modelBottom_Yup + (totalHeight * bottomCropPercentage);
        float visibleCenter_Yup = (modelTop_Yup + croppedBottom_Yup) / 2.0f;
        camera.Target.x = eyeCenter_Yup.x;
        camera.Target.y = visibleCenter_Yup;
        camera.Target.z = eyeCenter_Yup.z;
        float visibleHeight = modelTop_Yup - croppedBottom_Yup;
        float requiredHalfHeight = visibleHeight / 2.0f;
        float cameraDistance = requiredHalfHeight / tan(fovYRadians / 2.0f);
        camera.Radius = cameraDistance * 1.05f;
        camera.Yaw = 90.0f;
        camera.Pitch = 0.0f;
        camera.updateCameraVectors();

        std::cout << "  [Mugshot Debug] Camera Target (Y-up): " << glm::to_string(camera.Target) << std::endl;
        std::cout << "  [Mugshot Debug] Visible Height (Y-up): " << visibleHeight << std::endl;
        std::cout << "  [Mugshot Debug] Final Camera Radius: " << camera.Radius << std::endl;
        std::cout << "  [Mugshot Debug] Final Camera Position: " << glm::to_string(camera.Position) << std::endl;
        std::cout << "-------------------------------------\n" << std::endl;

    }
    else {
        std::cerr << "Renderer failed to load NIF model." << std::endl;
    }
}

void Renderer::loadCustomSkeleton(const std::string& path) {
    if (customSkeleton.loadFromFile(path)) {
        activeSkeleton = &customSkeleton;
        currentSkeletonType = SkeletonType::Custom;
    }
    else {
        std::cerr << "Failed to load custom skeleton. It will not be available." << std::endl;
    }
}

void Renderer::setCamera(float posX, float posY, float posZ, float pitch, float yaw) {
    camera.Position = glm::vec3(posX, posY, posZ);
    camera.Pitch = pitch;
    camera.Yaw = yaw;
    camera.updateCameraVectors();
}

void Renderer::setFallbackRootDirectory(const std::string& path) {
    fallbackRootDirectory = path;
    textureManager.setActiveDirectories(rootDirectory, fallbackRootDirectory);
    std::cout << "Fallback root directory set to: " << fallbackRootDirectory << std::endl;
    saveConfig();
}

void Renderer::saveToPNG(const std::string& path) {
    std::vector<unsigned char> buffer(screenWidth * screenHeight * 4);
    glReadPixels(0, 0, screenWidth, screenHeight, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.c_str(), screenWidth, screenHeight, 4, buffer.data(), screenWidth * 4)) {
        throw std::runtime_error("Failed to save image to " + path);
    }
}

void Renderer::loadConfig() {
    std::ifstream configFile(configPath);
    if (configFile.is_open()) {
        std::getline(configFile, currentNifPath);
        std::getline(configFile, fallbackRootDirectory);
        configFile.close();

        if (fallbackRootDirectory.empty()) {
            fallbackRootDirectory = "C:\\Games\\Steam\\steamapps\\common\\Skyrim Special Edition\\Data";
        }
    }
}

void Renderer::saveConfig() {
    std::ofstream configFile(configPath);
    if (configFile.is_open()) {
        configFile << currentNifPath << std::endl;
        configFile << fallbackRootDirectory << std::endl;
        configFile.close();
    }
    else {
        std::cerr << "Warning: Could not save config file to " << configPath << std::endl;
    }
}

// --- NEW PUBLIC HANDLER IMPLEMENTATIONS ---
void Renderer::HandleMouseButton(int button, int action, int mods) {
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isRotating = true;
            firstMouse = true;
        }
        else if (action == GLFW_RELEASE) {
            isRotating = false;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            isPanning = true;
            firstMouse = true;
        }
        else if (action == GLFW_RELEASE) {
            isPanning = false;
        }
    }
}

void Renderer::HandleCursorPosition(double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;

    if (isRotating) {
        camera.ProcessMouseOrbit(xoffset, yoffset);
    }
    if (isPanning) {
        camera.ProcessMousePan(xoffset, yoffset);
    }
}

void Renderer::HandleScroll(double xoffset, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    camera.ProcessMouseScroll(yoffset);
}

void Renderer::HandleKey(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_0 && mods == GLFW_MOD_CONTROL) {
            camera.Reset();
        }
        if (key == GLFW_KEY_LEFT) camera.ProcessKeyRotation(KeyRotation::LEFT);
        if (key == GLFW_KEY_RIGHT) camera.ProcessKeyRotation(KeyRotation::RIGHT);
        if (key == GLFW_KEY_UP) camera.ProcessKeyRotation(KeyRotation::UP);
        if (key == GLFW_KEY_DOWN) camera.ProcessKeyRotation(KeyRotation::DOWN);
    }
}

// --- GLOBAL CALLBACKS (now simple wrappers) ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    // This function will now call our new class method
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->HandleFramebufferSize(width, height);
    }
}
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) renderer->HandleMouseButton(button, action, mods);
}
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) renderer->HandleCursorPosition(xpos, ypos);
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) renderer->HandleScroll(xoffset, yoffset);
}
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) renderer->HandleKey(key, scancode, action, mods);
}

