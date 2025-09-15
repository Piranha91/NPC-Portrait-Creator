#define GLM_ENABLE_EXPERIMENTAL

#include "Renderer.h"
#include "BsaManager.h"
#include "Skeleton.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
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
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <chrono>

// By defining NOMINMAX, we prevent Windows.h from defining min() and max() macros,
// which conflict with the C++ standard library's std::min and std::max.
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h> // For IFileOpenDialog

#include "lodepng/lodepng.h"
#include "Version.h"

void checkGlErrors(const char* location) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "!!! OpenGL Error at " << location << ": " << err << "!!!" << std::endl;
    }
}


// --- Global Callback Prototypes ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
std::string selectFolderDialog_ModernWindows(const std::string& title);

Renderer::Renderer(int width, int height, const std::string& app_dir)
    : screenWidth(width), screenHeight(height),
    camera(glm::vec3(0.0f, 50.0f, 0.0f)),
    lastX(width / 2.0f), lastY(height / 2.0f),
    backgroundColor(0.227f, 0.239f, 0.251f),
    appDirectory(app_dir),
    assetManager(), // Default construct AssetManager
    textureManager(assetManager) // Pass the AssetManager reference to the TextureManager
{
    configPath = (std::filesystem::path(appDirectory) / "mugshotter_config.json").string();
}

Renderer::~Renderer() {
    if (!isHeadless) { 
        shutdownUI();
    }
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void Renderer::updateAssetManagerPaths() {
    // --- Assemble final list of paths for the AssetManager ---
    std::vector<std::filesystem::path> finalPaths;
    // 1. Prepend the GameDataDirectory to make it the lowest priority.
    if (!gameDataDirectory.empty()) {
        finalPaths.push_back(gameDataDirectory);
    }
    // 2. Append all user-specified data folders.
    for (const auto& s : dataFolders) {
        // Avoid adding duplicates if a user manually adds the game directory
        if (s != gameDataDirectory) {
            finalPaths.push_back(s);
        }
    }
    // 3. Pass the complete, prioritized list to the AssetManager.
    assetManager.setActiveDirectories(finalPaths, appDirectory);
}

void Renderer::init(bool headless) {
    this->isHeadless = headless;
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

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, screenWidth, screenHeight);
    shader.load("shaders/basic.vert", "shaders/basic.frag");

    // --- BSA and Skeleton Loading ---
    // Load config first to get the fallback directory
    loadConfig();

    // Initialize the AssetManager with all data folders.
    updateAssetManagerPaths();

    // --- Load all standard and beast skeletons using the AssetManager ---
    std::cout << "[Skeleton Load] Attempting to load all default skeletons...\n";

    const std::string femaleSkelPath = "meshes\\actors\\character\\character assets female\\skeleton_female.nif";
    auto femaleData = assetManager.extractFile(femaleSkelPath);
    if (!femaleData.empty()) femaleSkeleton.loadFromMemory(femaleData, "skeleton_female.nif");

    const std::string maleSkelPath = "meshes\\actors\\character\\character assets\\skeleton.nif";
    auto maleData = assetManager.extractFile(maleSkelPath);
    if (!maleData.empty()) maleSkeleton.loadFromMemory(maleData, "skeleton.nif");

    const std::string femaleBeastSkelPath = "meshes\\actors\\character\\character assets female\\skeletonbeast_female.nif";
    auto femaleBeastData = assetManager.extractFile(femaleBeastSkelPath);
    if (!femaleBeastData.empty()) femaleBeastSkeleton.loadFromMemory(femaleBeastData, "skeletonbeast_female.nif");

    const std::string maleBeastSkelPath = "meshes\\actors\\character\\character assets\\skeletonbeast.nif";
    auto maleBeastData = assetManager.extractFile(maleBeastSkelPath);
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

        // --- NEW: Check if a screenshot was requested ---
		// --- Note : This is done after rendering the frame but before swapping buffers to avoid capturing the UI ---
        if (!screenshotPath.empty()) {
            try {
                saveToPNG(screenshotPath);
                std::cout << "Image saved to " << screenshotPath << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Error saving PNG: " << e.what() << std::endl;
                tinyfd_messageBox("Error", e.what(), "ok", "error", 1);
            }
            screenshotPath.clear(); // Clear the path to avoid saving every frame.
        }
        // --- End of new code ---

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
                    dataFolders.clear();
                    nifLoadStartTime = std::chrono::high_resolution_clock::now();
                    newModelLoaded = true;

                    loadNifModel(filePath);
                }
            }
            if (ImGui::BeginMenu("Data Folders")) {
                if (ImGui::MenuItem("Add Folder...")) {
                    std::string folderPath = selectFolderDialog_ModernWindows("Select Data Folder");
                    if (!folderPath.empty()) { // Check for empty string on cancellation.
                        dataFolders.push_back(folderPath);
                        // Reload model to apply new data folders
                        loadNifModel("");
                    }
                }
                ImGui::Separator();
                ImGui::Text("Priority: Bottom is highest");
                ImGui::Separator();

                // List current data folders with options to reorder and remove
                for (int i = 0; i < dataFolders.size(); ++i) {
                    ImGui::PushID(i); // Create a unique ID scope for buttons

                    // MODIFICATION: Use ImGui::Text instead of ImGui::TextWrapped.
                    ImGui::Text("%d: %s", i, dataFolders[i].c_str());

                    // Reordering buttons (Up/Down)
                    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
                    if (i > 0) {
                        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                            std::swap(dataFolders[i], dataFolders[i - 1]);
                            loadNifModel("");
                        }
                    }
                    else { // Placeholder to keep alignment
                        ImGui::InvisibleButton("##up_space", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                    }

                    ImGui::SameLine();
                    if (i < dataFolders.size() - 1) {
                        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                            std::swap(dataFolders[i], dataFolders[i + 1]);
                            loadNifModel("");
                        }
                    }

                    // Remove button
                    ImGui::SameLine();
                    if (ImGui::Button("X")) {
                        dataFolders.erase(dataFolders.begin() + i);
                        loadNifModel("");
                        ImGui::PopID();
                        break; // Exit loop as vector was modified
                    }
                    ImGui::PopID();
                }

                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Set Game Data Directory...")) {
                std::string folderPath = selectFolderDialog_ModernWindows("Select Game Data Directory");
                if (!folderPath.empty()) {
                    gameDataDirectory = folderPath;
                    loadNifModel(""); // Reload to apply the change
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) { glfwSetWindowShouldClose(window, true); }
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
                    // Instead of saving immediately, set the path to request a save.
                    screenshotPath = filePath;
                }
            }
            if (ImGui::MenuItem("Process Directory...")) {
                processDirectory(); // <-- Call the new function here
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

    glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0f);
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

    std::cout << "--- [Debug Matrices] ---" << std::endl;
    std::cout << "  Camera Position: " << glm::to_string(camera.Position) << std::endl;
    std::cout << "  View Matrix:\n" << glm::to_string(view) << std::endl;
    std::cout << "  Projection Matrix:\n" << glm::to_string(projection) << std::endl;

    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    shader.setVec3("viewPos", camera.Position);

    if (model) {
        model->draw(shader, camera.Position);
    }

    checkGlErrors("end of renderFrame");
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
    // Allow calling with an empty path to trigger a reload of the current model
    if (!path.empty()) {
        currentNifPath = path;
    }
    if (currentNifPath.empty()) {
        return; // Nothing to load or reload
    }

    // --- This part is the same: update data folders and tell the AssetManager ---
    std::string nifRootDirectory;
    std::string pathLower = currentNifPath;
    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

    size_t meshesPos = pathLower.rfind("\\meshes\\");
    if (meshesPos == std::string::npos) {
        meshesPos = pathLower.rfind("/meshes/");
    }
    if (meshesPos != std::string::npos) {
        nifRootDirectory = currentNifPath.substr(0, meshesPos);
        auto it = std::find(dataFolders.begin(), dataFolders.end(), nifRootDirectory);
        if (it == dataFolders.end()) {
            dataFolders.push_back(nifRootDirectory);
        }
    }

    // --- Assemble final list of paths for the AssetManager ---
    updateAssetManagerPaths();

    // --- RECOMMENDED CHANGE: Load NIF data through the AssetManager ---
    std::cout << "[NIF Load] Extracting: " << currentNifPath << std::endl;
    std::vector<char> nifData = assetManager.extractFile(currentNifPath);

    if (nifData.empty()) {
        std::cerr << "Renderer failed to load NIF model data via AssetManager." << std::endl;
        return;
    }

    // Use the in-memory data for skeleton detection
    nifly::NifFile tempNif;
    std::stringstream nifStream(std::string(nifData.begin(), nifData.end()));
    if (tempNif.Load(nifStream) == 0) {
        detectAndSetSkeleton(tempNif);
    }
    else {
        std::cerr << "Could not pre-load NIF from memory for skeleton detection." << std::endl;
        activeSkeleton = nullptr;
        currentSkeletonType = SkeletonType::None;
    }

    if (!model) {
        model = std::make_unique<NifModel>();
    }

    textureManager.cleanup(); // clear the texture cache before reloading the model in case new data folders were added.

    // You will need to update NifModel::load to also accept a vector<char>
    if (model->load(nifData, currentNifPath, textureManager, activeSkeleton)) {
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

void Renderer::saveToPNG(const std::string& path) {
    if (imageXRes <= 0 || imageYRes <= 0) {
        throw std::runtime_error("Invalid image resolution for saving PNG.");
    }

    // --- 1. Capture and resize the image (same as before) ---
    float targetAspect = static_cast<float>(imageXRes) / static_cast<float>(imageYRes);
    float viewportAspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    int rectWidth, rectHeight;
    if (targetAspect > viewportAspect) {
        rectWidth = screenWidth;
        rectHeight = static_cast<int>(static_cast<float>(screenWidth) / targetAspect);
    }
    else {
        rectHeight = screenHeight;
        rectWidth = static_cast<int>(static_cast<float>(screenHeight) * targetAspect);
    }
    int rectX = (screenWidth - rectWidth) / 2;
    int rectY = (screenHeight - rectHeight) / 2;
    std::vector<unsigned char> screen_buffer(rectWidth * rectHeight * 4);

    glFinish();                     // Wait for GPU to finish drawing.
    glReadBuffer(GL_BACK);          // Set the read source to the back buffer.

    glReadPixels(rectX, rectY, rectWidth, rectHeight, GL_RGBA, GL_UNSIGNED_BYTE, screen_buffer.data());

    // (Optional but good practice) Restore the default read buffer
    glReadBuffer(GL_FRONT);

    std::vector<unsigned char> resized_buffer(imageXRes * imageYRes * 4);
    stbir_resize_uint8_srgb(screen_buffer.data(), rectWidth, rectHeight, 0,
        resized_buffer.data(), imageXRes, imageYRes, 0, STBIR_RGBA);

    // --- 2. Flip the image vertically (required by OpenGL's coordinate system) ---
    // LodePNG requires the image data to be in the correct top-to-bottom order.
    std::vector<unsigned char> flipped_buffer(imageXRes * imageYRes * 4);
    for (int y = 0; y < imageYRes; y++) {
        memcpy(flipped_buffer.data() + (imageXRes * (imageYRes - 1 - y) * 4),
            resized_buffer.data() + (imageXRes * y * 4),
            imageXRes * 4);
    }

    // ============================ NEW CODE BLOCK START ============================
    // --- 2b. Convert from Pre-multiplied to Straight Alpha ---
    // The pixel data from the OpenGL framebuffer is pre-multiplied. We need to
    // "un-premultiply" it for standard PNGs that WPF expects.
    for (size_t i = 0; i < flipped_buffer.size(); i += 4) {
        // The buffer format is RGBA
        float r = flipped_buffer[i];
        float g = flipped_buffer[i + 1];
        float b = flipped_buffer[i + 2];
        float a = flipped_buffer[i + 3];

        if (a > 0) {
            // To reverse pre-multiplication, divide the color by the alpha.
            // Alpha is in the [0, 255] range, so we normalize it to [0, 1] for the division.
            float alpha_normal = a / 255.0f;
            r /= alpha_normal;
            g /= alpha_normal;
            b /= alpha_normal;

            // Clamp values to 255 and write back to the buffer
            flipped_buffer[i] = static_cast<unsigned char>(std::min(r, 255.0f));
            flipped_buffer[i + 1] = static_cast<unsigned char>(std::min(g, 255.0f));
            flipped_buffer[i + 2] = static_cast<unsigned char>(std::min(b, 255.0f));
        }
    }
    // ============================= NEW CODE BLOCK END =============================

    // --- 3. Create JSON metadata ---
    nlohmann::json metadata;
    metadata["program_version"] = PROGRAM_VERSION;
    metadata["resolution_x"] = imageXRes;
    metadata["resolution_y"] = imageYRes;
    metadata["camera"] = {
        {"pos_x", camX}, {"pos_y", camY}, {"pos_z", camZ},
        {"pitch", camPitch}, {"yaw", camYaw}
    };
    metadata["mugshot_offsets"] = {
        {"top", headTopOffset}, {"bottom", headBottomOffset}
    };
    std::string metadata_string = metadata.dump(4); // pretty-print with 4-space indent

    // --- 4. Encode and save the PNG with LodePNG ---
    std::vector<unsigned char> png_buffer;
    lodepng::State state;

    // Disable text compression to ensure a standard 'tEXt' chunk is created.
    state.encoder.text_compression = 0;

    // Add our metadata as a "tEXt" chunk.
    lodepng_add_text(&state.info_png, "Parameters", metadata_string.c_str());

    unsigned error = lodepng::encode(png_buffer, flipped_buffer, imageXRes, imageYRes, state);

    // No need to call lodepng_state_cleanup(&state); the State destructor handles it automatically.

    if (error) {
        throw std::runtime_error("LodePNG encoding error: " + std::string(lodepng_error_text(error)));
    }

    error = lodepng::save_file(png_buffer, path);
    if (error) {
        throw std::runtime_error("LodePNG file saving error: " + std::string(lodepng_error_text(error)));
    }
}

// This helper function creates and shows a modern folder selection dialog
std::string selectFolderDialog_ModernWindows(const std::string& title) {
    std::string folderPath = "";
    // 1. Initialize the COM library
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (SUCCEEDED(hr)) {
        IFileOpenDialog* pFileDialog = NULL;

        // 2. Create an instance of the FileOpenDialog
        hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileDialog));

        if (SUCCEEDED(hr)) {
            // 3. Set options to make it a folder picker
            DWORD dwOptions;
            pFileDialog->GetOptions(&dwOptions);
            pFileDialog->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

            // Set a custom title
            // Note: We need to convert our std::string title to a wide string (wchar_t*) for the API
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, &title[0], (int)title.size(), NULL, 0);
            std::wstring wtitle(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, &title[0], (int)title.size(), &wtitle[0], size_needed);
            pFileDialog->SetTitle(wtitle.c_str());

            // 4. Show the dialog
            hr = pFileDialog->Show(NULL);

            if (SUCCEEDED(hr)) {
                // 5. Get the result
                IShellItem* pItem;
                hr = pFileDialog->GetResult(&pItem);
                if (SUCCEEDED(hr)) {
                    PWSTR pszFilePath;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                    // 6. Convert the wide-char path back to a std::string
                    if (SUCCEEDED(hr)) {
                        int bufferSize = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                        if (bufferSize > 0) {
                            std::string path_str(bufferSize - 1, 0);
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &path_str[0], bufferSize, NULL, NULL);
                            folderPath = path_str;
                        }
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileDialog->Release();
        }
        // 7. Uninitialize the COM library
        CoUninitialize();
    }
    return folderPath;
}

void Renderer::processDirectory() {
    // 1. Prompt for the input directory using the new function
    std::string inputPath = selectFolderDialog_ModernWindows("Select Input Directory with NIF files");
    if (inputPath.empty()) {
        return; // User canceled
    }

    std::vector<std::filesystem::path> nifFiles;
    std::cout << "--- Scanning for .nif files in: " << inputPath << " ---" << std::endl;

    // 2. Find all .nif files in the selected directory (case-insensitively)
    for (const auto& entry : std::filesystem::directory_iterator(inputPath)) {
        if (entry.is_regular_file()) {
            // Get the extension as a string
            std::string extension = entry.path().extension().string();
            // Convert the extension to lowercase
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return std::tolower(c); });

            // Now, perform the case-insensitive check
            if (extension == ".nif") {
                nifFiles.push_back(entry.path());
            }
        }
    }

    // 3. Validate that NIF files were found
    if (nifFiles.empty()) {
        tinyfd_messageBox("Process Directory", "No .nif files found in the selected directory.", "ok", "info", 1);
        return;
    }

    // 4. Prompt for the output directory using the new function
    std::string outputPathStr = selectFolderDialog_ModernWindows("Select Output Directory for PNG files");
    if (outputPathStr.empty()) {
        return; // User canceled
    }
    std::filesystem::path outputPath = outputPathStr;

    // 5. Process each NIF file
    std::cout << "--- Starting batch process for " << nifFiles.size() << " files. The UI will be unresponsive. ---" << std::endl;
    for (const auto& nifPath : nifFiles) {
        std::cout << "Processing: " << nifPath.filename().string() << std::endl;

        // Load the model. This also sets up the automatic "mugshot" camera.
        loadNifModel(nifPath.string());

        // Explicitly render one frame with the new model
        renderFrame();

        // Construct the output path and save the PNG
        std::filesystem::path pngPath = outputPath / nifPath.filename().replace_extension(".png");
        saveToPNG(pngPath.string());

        // Update the screen and process events to keep the window from freezing entirely
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Notify user of completion
    std::string completionMessage = "Batch process complete. " + std::to_string(nifFiles.size()) + " files were exported.";
    tinyfd_messageBox("Process Complete", completionMessage.c_str(), "ok", "info", 1);
    std::cout << "--- Batch process complete. ---" << std::endl;
}

void Renderer::setDataFolders(const std::vector<std::string>& folders) {
    dataFolders = folders;
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

        gameDataDirectory = data.value("game_data_directory", "");

        // MODIFICATION: Load data folders list from config, with backwards compatibility.
        dataFolders.clear();
        if (data.contains("data_folders")) {
            data.at("data_folders").get_to(dataFolders);
        }
        else if (data.contains("fallback_root_directory")) {
            // Read old key for backwards compatibility
            std::string fallbackDir = data.value("fallback_root_directory", "");
            if (!fallbackDir.empty()) {
                dataFolders.push_back(fallbackDir);
            }
        }

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

        if (data.contains("background_color") && data["background_color"].is_array() && data["background_color"].size() == 3) {
            backgroundColor.r = data["background_color"][0].get<float>();
            backgroundColor.g = data["background_color"][1].get<float>();
            backgroundColor.b = data["background_color"][2].get<float>();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
    }
}

void Renderer::saveConfig() {
    try {
        nlohmann::json data;
        data["last_nif_path"] = currentNifPath;
        data["data_folders"] = dataFolders;
        data["game_data_directory"] = gameDataDirectory;

        data["camX"] = camX;
        data["camY"] = camY;
        data["camZ"] = camZ;
        data["pitch"] = camPitch;
        data["yaw"] = camYaw;

        data["head_top_offset"] = headTopOffset;
        data["head_bottom_offset"] = headBottomOffset;

        data["image_resolution_x"] = imageXRes;
        data["image_resolution_y"] = imageYRes;

        data["background_color"] = { backgroundColor.r, backgroundColor.g, backgroundColor.b };

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

