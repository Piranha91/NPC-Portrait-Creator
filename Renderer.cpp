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
#include <nlohmann/json.hpp>
#include <iomanip> // For std::setw

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

#include <chrono>

// --- Global Callback Prototypes ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

Renderer::Renderer(int width, int height, const std::string& app_dir)
    : screenWidth(width), screenHeight(height),
    camera(glm::vec3(0.0f, 50.0f, 0.0f)),
    lastX(width / 2.0f), lastY(height / 2.0f),
    fallbackRootDirectory("C:\\Games\\Steam\\steamapps\\common\\Skyrim Special Edition\\Data"),
    appDirectory(app_dir) {

    configPath = (std::filesystem::path(appDirectory) / "mugshotter_config.json").string();
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

    glfwWindowHint(GLFW_SAMPLES, 4); // Request 4x MSAA
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
    bsaManager.loadArchives(fallbackRootDirectory, appDirectory);

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
                // Start the timer for the auto-loaded model
                nifLoadStartTime = std::chrono::high_resolution_clock::now();
                newModelLoaded = true;

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

        // --- STOP the timer and log the total duration ---
        if (newModelLoaded) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - nifLoadStartTime);
            std::cout << "\n--- [Total Load Time] From file selection to first render: "
                << duration.count() << " ms ---\n" << std::endl;
            newModelLoaded = false; // Reset the flag for the next time
        }

        glfwSwapBuffers(window);
    }
}

void Renderer::renderUI() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open NIF...")) {
                const char* filterPatterns[1] = { "*.nif" };
                const char* filePath = tinyfd_openFileDialog("Open NIF File", "", 1, filterPatterns, "NIF Files", 0);
                if (filePath) {
                    nifLoadStartTime = std::chrono::high_resolution_clock::now();
                    newModelLoaded = true;

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

        if (ImGui::BeginMenu("Image")) {
            if (ImGui::MenuItem("Save PNG...")) {
                const char* filterPatterns[1] = { "*.png" };
                const char* filePath = tinyfd_saveFileDialog("Save Image", "output.png", 1, filterPatterns, "PNG Files");
                if (filePath) {
                    try {
                        saveToPNG(filePath);
                        std::cout << "Image saved to " << filePath << std::endl;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Error saving PNG: " << e.what() << std::endl;
                        tinyfd_messageBox("Error", e.what(), "ok", "error", 1);
                    }
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
    if (screenWidth == 0 || screenHeight == 0) {
        return;
    }

    glClearColor(0.227f, 0.239f, 0.251f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader.use();
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)screenWidth / (float)screenHeight, 10.0f, 10000.0f);
    glm::mat4 view = camera.GetViewMatrix();

    glm::mat4 conversionMatrix = glm::mat4(
        -1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    view = view * conversionMatrix;

    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    shader.setVec3("viewPos", camera.Position);

    if (model) {
        model->draw(shader, camera.Position);
    }
}

void Renderer::HandleFramebufferSize(int width, int height) {
    screenWidth = width;
    screenHeight = height;
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

    for (const auto* shape : shapes) {
        if (auto* triShape = dynamic_cast<const nifly::BSTriShape*>(shape)) {
            checkString(triShape->name.get());
        }
    }

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
    textureManager.setActiveDirectories(rootDirectory, fallbackRootDirectory, appDirectory);

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

        // Check which camera mode to use. Mugshot mode is used only if all absolute camera parameters are zero.
        bool useAbsoluteCamera = (camX != 0.0f || camY != 0.0f || camZ != 0.0f || camPitch != 0.0f || camYaw != 0.0f);

        if (useAbsoluteCamera) {
            std::cout << "\n--- Using Absolute Camera Position ---\n";
            camera.Position = glm::vec3(camX, camY, camZ);
            camera.Pitch = camPitch;
            camera.Yaw = camYaw;
            camera.updateCameraVectors();
            std::cout << "  [Camera Debug] Position set to: (" << camX << ", " << camY << ", " << camZ << ")\n";
            std::cout << "  [Camera Debug] Rotation set to: Pitch=" << camPitch << ", Yaw=" << camYaw << "\n";
            std::cout << "-------------------------------------\n" << std::endl;
        }

        else
        {
            std::cout << "\n--- Calculating Mugshot Camera Position ---\n";
            std::cout << "  [Mugshot Config] headTopOffset: " << headTopOffset << " (" << headTopOffset * 100.0f << "%)\n";
            std::cout << "  [Mugshot Config] headBottomOffset: " << headBottomOffset << " (" << headBottomOffset * 100.0f << "%)\n";

            // 1. Get bounds from the model, preferring the specific head shape
            glm::vec3 headMinBounds_Zup;
            glm::vec3 headMaxBounds_Zup;

            // --- MODIFICATION START: Prioritize partition bounds, then fall back ---
            if (model->hasHeadShapeBounds()) {
                headMinBounds_Zup = model->getHeadShapeMinBounds();
                headMaxBounds_Zup = model->getHeadShapeMaxBounds();
                std::cout << "  [Mugshot Info] Using specific head partition bounds for framing.\n";
            }
            else {
                // Fallback for models with no head partition
                headMinBounds_Zup = model->getHeadMinBounds();
                headMaxBounds_Zup = model->getHeadMaxBounds();
                std::cout << "  [Mugshot Warning] No head partition found. Falling back to aggregate head bounds.\n";
            }
            // --- MODIFICATION END ---

            // 2. Convert coordinates from Skyrim's Z-up to our renderer's Y-up
            float headTop_Yup = headMaxBounds_Zup.z;
            float headBottom_Yup = headMinBounds_Zup.z;

            // Calculate horizontal center based on HEAD bounds to ensure a straight-on view
            float headCenterX_Yup = -(headMinBounds_Zup.x + headMaxBounds_Zup.x) / 2.0f;
            // --- MODIFICATION START: Invert the Y-axis to correctly map depth ---
            float headCenterZ_Yup = -(headMinBounds_Zup.y + headMaxBounds_Zup.y) / 2.0f;
            // --- MODIFICATION END ---

            // 3. Define the vertical frame for the mugshot based on the HEAD MESH ONLY
            float headHeight = headTop_Yup - headBottom_Yup;

            // --- MODIFICATION START: Use configurable offsets ---
            float frameBottom_Yup = headBottom_Yup + (headHeight * headBottomOffset); // Apply bottom offset
            float frameTop_Yup = headTop_Yup + (headHeight * headTopOffset); // Apply top offset
            // --- MODIFICATION END ---

            float frameHeight = frameTop_Yup - frameBottom_Yup;
            float frameCenterY = (frameTop_Yup + frameBottom_Yup) / 2.0f;

            // 4. Calculate required camera distance based on the vertical frame ONLY
            const float fovYRadians = glm::radians(45.0f);
            float distanceForHeight = (frameHeight / 2.0f) / tan(fovYRadians / 2.0f);
            // 5. Set camera properties
            camera.Radius = distanceForHeight;
            camera.Target = glm::vec3(headCenterX_Yup, frameCenterY, headCenterZ_Yup);
            camera.Yaw = 90.0f; // Use 90 for a direct front-on view
            camera.Pitch = 0.0f;
            camera.updateCameraVectors();

            std::cout << "  [Mugshot Debug] Camera Target (Y-up): " << glm::to_string(camera.Target) << std::endl;
            std::cout << "  [Mugshot Debug] Visible Height (Y-up): " << frameHeight << std::endl;
            std::cout << "  [Mugshot Debug] Final Camera Radius: " << camera.Radius << std::endl;
            std::cout << "  [Mugshot Debug] Final Camera Position: " << glm::to_string(camera.Position) << std::endl;
            std::cout << "-------------------------------------\n" << std::endl;
        }
    }
    else {
        std::cerr << "Renderer failed to load NIF model."
            << std::endl;
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

void Renderer::setFallbackRootDirectory(const std::string& path) {
    fallbackRootDirectory = path;
    textureManager.setActiveDirectories(rootDirectory, fallbackRootDirectory, appDirectory);
    std::cout << "Fallback root directory set to: " << fallbackRootDirectory << std::endl;
    saveConfig();
}

void Renderer::saveToPNG(const std::string& path) {
    if (imageXRes <= 0 || imageYRes <= 0) {
        throw std::runtime_error("Invalid image resolution for saving PNG.");
    }

    // Determine the aspect ratio of the rectangle to save
    float targetAspect = static_cast<float>(imageXRes) / static_cast<float>(imageYRes);
    float viewportAspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    int rectWidth, rectHeight;
    // Calculate the dimensions of the largest possible rectangle with the target aspect ratio
    // that fits within the current screen/viewport.
    if (targetAspect > viewportAspect) {
        // Target is wider than viewport (letterbox): constrained by viewport width
        rectWidth = screenWidth;
        rectHeight = static_cast<int>(static_cast<float>(screenWidth) / targetAspect);
    }
    else {
        // Target is narrower than viewport (pillarbox): constrained by viewport height
        rectHeight = screenHeight;
        rectWidth = static_cast<int>(static_cast<float>(screenHeight) * targetAspect);
    }

    // Calculate the centered starting position for glReadPixels
    int rectX = (screenWidth - rectWidth) / 2;
    int rectY = (screenHeight - rectHeight) / 2;

    std::vector<unsigned char> buffer(rectWidth * rectHeight * 4);
    glReadPixels(rectX, rectY, rectWidth, rectHeight, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
    stbi_flip_vertically_on_write(1);
    // The output PNG will have the dimensions of the captured rectangle
    if (!stbi_write_png(path.c_str(), rectWidth, rectHeight, 4, buffer.data(), rectWidth * 4)) {
        throw std::runtime_error("Failed to save image to " + path);
    }
}

void Renderer::loadConfig() {
    if (!std::filesystem::exists(configPath)) return;

    try {
        std::ifstream f(configPath);
        nlohmann::json data = nlohmann::json::parse(f, nullptr, false);
        if (data.is_discarded()) {
            std::cerr << "Warning: Could not parse config file " << configPath << std::endl;
            return;
        }

        currentNifPath = data.value("last_nif_path", "");
        fallbackRootDirectory = data.value("fallback_root_directory", "C:\\Games\\Steam\\steamapps\\common\\Skyrim Special Edition\\Data");

        // Load camera settings
        camX = data.value("camX", 0.0f);
        camY = data.value("camY", 0.0f);
        camZ = data.value("camZ", 0.0f);
        camPitch = data.value("pitch", 0.0f);
        camYaw = data.value("yaw", 0.0f);

        // Load mugshot settings
        headTopOffset = data.value("head_top_offset", 0.20f);
        headBottomOffset = data.value("head_bottom_offset", -0.05f);

        // Load image settings
        imageXRes = data.value("image_resolution_x", 1280);
        imageYRes = data.value("image_resolution_y", 720);

    }
    catch (const std::exception& e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
    }
}

void Renderer::saveConfig() {
    try {
        nlohmann::json data;
        data["last_nif_path"] = currentNifPath;
        data["fallback_root_directory"] = fallbackRootDirectory;

        data["camX"] = camX;
        data["camY"] = camY;
        data["camZ"] = camZ;
        data["pitch"] = camPitch;
        data["yaw"] = camYaw;

        data["head_top_offset"] = headTopOffset;
        data["head_bottom_offset"] = headBottomOffset;

        data["image_resolution_x"] = imageXRes;
        data["image_resolution_y"] = imageYRes;

        std::ofstream o(configPath);
        o << std::setw(4) << data << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: Could not save config file to " << configPath << ": " << e.what() << std::endl;
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
        camera.ProcessMousePan(xoffset, ypos);
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

