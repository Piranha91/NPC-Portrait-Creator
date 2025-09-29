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
#include <iomanip> // For std::setw, std::setfill
#include <sstream> // For std::stringstream
#include <array>   // For std::array

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX               // <-- prevents min/max macros
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <GLFW/glfw3native.h>
#endif
#define GLFW_EXPOSE_NATIVE_WIN32          
#include <GLFW/glfw3native.h>             
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp> // For logging glm::vec3
#include <glm/gtx/quaternion.hpp>

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

#include <openssl/evp.h>

// Helper function to compute SHA256 hash
std::array<unsigned char, 32> sha256(const void* data, size_t len) {
    std::array<unsigned char, 32> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int outlen = 0;
    EVP_DigestFinal_ex(ctx, out.data(), &outlen);
    EVP_MD_CTX_free(ctx);
    return out;
}

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
    assetManager(),
    textureManager(assetManager),
    // CORRECTED: Use only one set of {} braces inside the () parentheses
    m_arrowVertices({
    // Shaft
    {0.0f, 0.0f,  0.5f}, {0.0f, 0.0f, -0.5f},
    // Head
    {0.0f, 0.0f, -0.5f}, { 0.2f,  0.2f, -0.2f},
    {0.0f, 0.0f, -0.5f}, {-0.2f,  0.2f, -0.2f},
    {0.0f, 0.0f, -0.5f}, {-0.2f, -0.2f, -0.2f},
    {0.0f, 0.0f, -0.5f}, { 0.2f, -0.2f, -0.2f}
        }) 
    {
        configPath = (std::filesystem::path(appDirectory) / "NPC_Portrait_Creator.json").string();
    }

Renderer::~Renderer() {
    glDeleteVertexArrays(1, &m_arrowVAO);
    glDeleteBuffers(1, &m_arrowVBO);

    if (uiInitialized) {
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
    window = glfwCreateWindow(screenWidth, screenHeight, "NPC Portrait Creator", NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwMakeContextCurrent(window);

    // --- Set Program Icon ---
    #ifdef _WIN32
    #ifndef IDI_APP_ICON
    #define IDI_APP_ICON 101
    #endif
        HWND hwnd = glfwGetWin32Window(window);

        // load 32px & 16px versions from the embedded icon resource
        HICON hIconBig = (HICON)LoadImage(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDI_APP_ICON),
            IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);

        HICON hIconSmall = (HICON)LoadImage(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDI_APP_ICON),
            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

        // apply to the window & taskbar
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    #endif


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

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, screenWidth, screenHeight);
    shader.load("shaders/basic.vert", "shaders/basic.frag");
    depthShader.load("shaders/depth_shader.vert", "shaders/depth_shader.frag");
    m_debugLineShader.load("shaders/debug_line.vert", "shaders/debug_line.frag"); // <-- LOAD THE NEW SHADER

    glGenVertexArrays(1, &m_arrowVAO);
    glGenBuffers(1, &m_arrowVBO);

    glBindVertexArray(m_arrowVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_arrowVBO);
    glBufferData(GL_ARRAY_BUFFER, m_arrowVertices.size() * sizeof(glm::vec3), m_arrowVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    // --- END ARROW SETUP ---

    // --- NEW: Create Framebuffer for Shadow Map ---
    glGenFramebuffers(1, &depthMapFBO);

    glGenTextures(1, &depthMapTexture);
    glBindTexture(GL_TEXTURE_2D, depthMapTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMapTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // --- END NEW ---

    // ## Only load profile from file if one hasn't been set by command line already ##
    if (lights.empty()) {
        loadLightingProfile(lightingProfilePath);
    }

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
    uiInitialized = true;
}

void Renderer::run() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. Start a new ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 2. Build your UI. This now happens BEFORE you render the 3D scene.
        renderUI();

        // 3. Render the 3D scene in the background. This function ALSO
        //    creates the light interaction overlay window for ImGui.
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        renderFrame();

        // 4. NOW that all UI is defined, finalize the ImGui draw data.
        ImGui::Render();

        // (Your screenshot logic can stay here)
        if (!screenshotPath.empty()) {
            try {
                saveToPNG(screenshotPath);
                std::cout << "Image saved to " << screenshotPath << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Error saving PNG: " << e.what() << std::endl;
                tinyfd_messageBox("Error", e.what(), "ok", "error", 1);
            }
            screenshotPath.clear();
        }

        // 5. Render the ImGui draw data ON TOP of the 3D scene.
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // (Your timer logic can stay here)
        if (newModelLoaded) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - nifLoadStartTime);
            std::cout << "\n--- [Total Load Time] From file selection to first render: "
                << duration.count() << " ms ---\n" << std::endl;
            newModelLoaded = false;
        }

        // 6. Swap buffers to show the final image.
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

            if (ImGui::MenuItem("Load Lighting Profile...")) {
                const char* filterPatterns[1] = { "*.json" };
                const char* filePath = tinyfd_openFileDialog("Open Lighting Profile", "", 1, filterPatterns, "JSON Files", 0);
                if (filePath) {
                    loadLightingProfile(filePath);
                    lightingProfilePath = filePath; // Save the new path to be remembered
                    saveConfig();
                }
            }

            if (ImGui::MenuItem("Save Lighting Profile")) {
                if (!lightingProfilePath.empty()) {
                    saveLightingProfile(lightingProfilePath);
                }
                else {
                    // If no file is loaded, prompt for a new one
                    const char* filterPatterns[1] = { "*.json" };
                    const char* filePath = tinyfd_saveFileDialog("Save Lighting Profile", "lighting.json", 1, filterPatterns, "JSON Files");
                    if (filePath) {
                        lightingProfilePath = filePath;
                        saveConfig();
                        saveLightingProfile(lightingProfilePath);
                    }
                }
            }

            if (ImGui::MenuItem("Clear Lighting Profile")) {
                loadLightingProfile(""); // Calling with an empty path loads the default
                lightingProfilePath = "";  // Clear the saved path
                saveConfig();
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

        if (ImGui::BeginMenu("Lighting")) {
            ImGui::SeparatorText("Ambient Light");

            // Find the first ambient light to control
            Light* ambientLight = nullptr;
            auto it = std::find_if(lights.begin(), lights.end(), [](const Light& l) { return l.type == 1; });
            if (it != lights.end()) {
                ambientLight = &(*it);
            }

            if (ambientLight) {
                // If an ambient light exists, show controls for it
                ImGui::ColorEdit3("Color", &ambientLight->color.r);
                ImGui::DragFloat("Intensity", &ambientLight->intensity, 0.01f, 0.0f, 10.0f);
            }
            else {
                // If no ambient light exists, show a button to add one
                if (ImGui::Button("Add Ambient Light")) {
                    Light newLight;
                    newLight.type = 1; // Ambient type
                    newLight.color = glm::vec3(0.15f, 0.15f, 0.15f);
                    newLight.intensity = 1.0f;
                    lights.push_back(newLight);
                }
            }

            ImGui::SeparatorText("Directional Lights");
            ImGui::Checkbox("Edit Directional Lights", &m_visualizeLights);

            ImGui::EndMenu();
        }

        if (model && ImGui::BeginMenu("View")) {
            ImGui::SeparatorText("Camera");

            // When the slider is moved, recalculate camera distance to maintain framing
            if (ImGui::SliderFloat("Field of View", &m_cameraFovY, 10.0f, 90.0f, "%.1f deg")) {
                if (m_mugshotFrameHeight > 0.0f) {
                    const float fovYRadians = glm::radians(m_cameraFovY);
                    camera.Radius = (m_mugshotFrameHeight / 2.0f) / tan(fovYRadians / 2.0f);
                    camera.updateCameraVectors();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                m_cameraFovY = 25.0f; // Reset to default
                if (m_mugshotFrameHeight > 0.0f) {
                    const float fovYRadians = glm::radians(m_cameraFovY);
                    camera.Radius = (m_mugshotFrameHeight / 2.0f) / tan(fovYRadians / 2.0f);
                    camera.updateCameraVectors();
                }
            }
            ImGui::Separator();
            // We removed ImGuiTreeNodeFlags_DefaultOpen to make it start collapsed.
            if (ImGui::CollapsingHeader("Mesh Parts")) {

                // Helper lambda to create checkboxes for a vector of shapes
                auto create_checkboxes = [](const char* group_name, std::vector<MeshShape>& shapes) {
                    if (shapes.empty()) {
                        return;
                    }

                    if (ImGui::TreeNode(group_name)) {
                        ImGui::PushID(group_name); // Unique ID scope for buttons

                        if (ImGui::Button("Show All")) {
                            for (auto& shape : shapes) {
                                shape.visible = true;
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Hide All")) {
                            for (auto& shape : shapes) {
                                shape.visible = false;
                            }
                        }
                        ImGui::Separator();

                        ImGui::PopID(); // End button scope

                        for (size_t i = 0; i < shapes.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            ImGui::Checkbox(shapes[i].name.c_str(), &shapes[i].visible);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    };

                create_checkboxes("Opaque Parts", model->getOpaqueShapes());
                create_checkboxes("Alpha-Test Parts", model->getAlphaTestShapes());
                create_checkboxes("Transparent Parts", model->getTransparentShapes());
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

    // --- 1. DEPTH PASS (Render scene from light's perspective) ---
    glm::mat4 lightProjection, lightView;
    glm::mat4 lightSpaceMatrix;
    float near_plane = 1.0f, far_plane = 1500.0f; // Adjust these values to fit your scene

    // Find the primary directional light to use for shadows
    glm::vec3 lightDir = glm::vec3(-0.5f, -0.5f, -1.0f); // Default fallback
    for (const auto& light : lights) {
        if (light.type == 2) { // type 2 is directional
            lightDir = light.direction;
            break;
        }
    }

    lightProjection = glm::ortho(-500.0f, 500.0f, -500.0f, 500.0f, near_plane, far_plane);
    lightView = glm::lookAt(-lightDir * 500.0f, glm::vec3(0.0f), glm::vec3(0.0, 1.0, 0.0));
    lightSpaceMatrix = lightProjection * lightView;

    depthShader.use();
    depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    if (model) {
        model->drawDepthOnly(depthShader);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);


    // --- 2. MAIN RENDER PASS (Render scene normally from camera's perspective) ---
    glViewport(0, 0, screenWidth, screenHeight);
    glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader.use();

    // --- SET LIGHT UNIFORMS ---
    // This is the conversion matrix that correctly transforms the model for rendering.
    // We will now use it for the light direction as well.
    glm::mat4 conversionMatrix = glm::mat4(
        -1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    int maxLights = 5; // Should match shader's MAX_LIGHTS
    for (int i = 0; i < maxLights; ++i) {
        std::string base = "lights[" + std::to_string(i) + "]";
        if (i < lights.size()) {
            shader.setInt(base + ".type", lights[i].type);
            // ============================ FIX START ============================
            // Send the ORIGINAL, UNMODIFIED light direction to the shader.
            // The special 'view' uniform already contains the conversion matrix,
            // so it will correctly transform both the model's normals AND the
            // light's direction in the same way.
            // We still negate the vector to get the direction *to* the light source.
            shader.setVec3(base + ".direction", -lights[i].direction);
            // ============================= FIX END =============================

            shader.setVec3(base + ".color", lights[i].color);
            shader.setFloat(base + ".intensity", lights[i].intensity);
        }
        else {
            shader.setInt(base + ".type", 0); // Disable unused lights
        }
    }

    glm::mat4 projection = glm::perspective(glm::radians(m_cameraFovY), (float)screenWidth / (float)screenHeight, 10.0f, 10000.0f);
    // 1. Get the original, unmodified view matrix from the camera
    glm::mat4 originalView = camera.GetViewMatrix();

    // 2. Create a modified view matrix specifically for the NIF model
    glm::mat4 modelView = originalView;
    modelView = modelView * conversionMatrix;

    // --- END OF CHANGES ---

    shader.setMat4("projection", projection);
    shader.setMat4("view", modelView); // <-- Use the MODIFIED view for the main model
    shader.setVec3("viewPos", camera.Position);
    shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, depthMapTexture);
    shader.setInt("shadowMap", 8);

    if (model) {
        model->draw(shader, camera.Position);
    }

    if (m_visualizeLights && !m_visualizeLights_lastState) {
        // All console output now goes inside this block, so it only runs once.
        int lightIndex = 0;
        std::cout << "\n--- Light Visualization Enabled: Calculation Details ---" << std::endl;
        for (const auto& light : lights) {
            if (light.type == 2) {
                std::cout << "--- Processing Arrow for Light #" << lightIndex + 1 << " ---" << std::endl;
                std::cout << "  [Input] Raw Direction: " << glm::to_string(light.direction) << std::endl;
                std::cout << "  [Input] Raw Color:     " << glm::to_string(light.color) << std::endl;
                std::cout << "  [Input] Raw Intensity: " << light.intensity << std::endl;

                glm::vec3 transformedDir = glm::vec3(conversionMatrix * glm::vec4(light.direction, 0.0f));
                std::cout << "  [Calc] Transformed Dir: " << glm::to_string(transformedDir) << std::endl;

                glm::vec3 arrowPos = camera.Target - (transformedDir * 50.0f);
                std::cout << "  [Calc] Arrow Position:  " << glm::to_string(arrowPos) << std::endl;

                float arrowLength = 20.0f * light.intensity;
                std::cout << "  [Calc] Arrow Length:    " << arrowLength << std::endl;

                lightIndex++;
            }
        }
        std::cout << "--------------------------------------------------------" << std::endl;

        // 1. Define the current view-projection matrix for calculations
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)screenWidth / (float)screenHeight, 10.0f, 10000.0f);
        glm::mat4 viewProjection = proj * view;

        // 2. We will find the single maximum zoom factor required.
        float maxZoomFactor = 1.0f;

        // 3. Pre-calculate the UI box padding in NDC space
        float box_half_width_ndc = 16.0f / (screenWidth / 2.0f);
        float box_half_height_ndc = 16.0f / (screenHeight / 2.0f);

        for (const auto& light : lights) {
            if (light.type == 2) {
                // --- CRITICAL: Please ensure your matrix definition matches this exactly ---
                glm::mat4 conversionMatrix = glm::mat4(
                    -1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );

                // Calculate the arrow's full model matrix, just like the drawing logic
                glm::vec3 transformedDir = glm::normalize(glm::vec3(conversionMatrix * glm::vec4(light.direction, 0.0f)));
                glm::vec3 arrowPos = camera.Target - (transformedDir * 50.0f);
                glm::mat4 modelMatrix =
                    glm::translate(glm::mat4(1.0f), arrowPos) *
                    glm::mat4_cast(glm::rotation(glm::vec3(0.0f, 0.0f, -1.0f), transformedDir)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(20.0f * light.intensity));

                // Helper lambda to get the required zoom for a single 3D point
                auto getRequiredZoomForPoint = [&](const glm::vec3& worldPos) -> float {
                    glm::vec4 clipPos = viewProjection * glm::vec4(worldPos, 1.0f);

                    if (clipPos.w <= 0.0f) { return 1.0f; }

                    glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
                    float requiredX = abs(ndcPos.x) + box_half_width_ndc;
                    float requiredY = abs(ndcPos.y) + box_half_height_ndc;
                    return std::max(requiredX, requiredY);
                    };

                // --- NEW: Iterate over every single vertex of the arrow model ---
                for (const auto& localVertex : m_arrowVertices) {
                    glm::vec3 worldPos = glm::vec3(modelMatrix * glm::vec4(localVertex, 1.0));
                    maxZoomFactor = std::max(maxZoomFactor, getRequiredZoomForPoint(worldPos));
                }
            }
        }

        // 5. If the max required zoom is greater than 1.0, apply it
        if (maxZoomFactor > 1.0f) {
            float finalZoomFactor = maxZoomFactor * 1.25f;

            std::cout << "[Auto-Zoom] Arrows out of view. Zooming out by a factor of "
                << finalZoomFactor << " (base: " << maxZoomFactor << " * 1.25 margin)." << std::endl;

            camera.Radius *= finalZoomFactor;
            camera.updateCameraVectors();
        }
    }

    // The drawing logic remains here, so the arrows are drawn every frame
    if (m_visualizeLights) {
        // We create a transparent, full-screen window to host the invisible buttons
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("LightInteractionOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // --- NEW: Logging Logic ---
        // Check if a right-click occurred anywhere in this overlay window
        bool rightClickHappened = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        // This flag will track if the click landed on a button
        bool clickWasOnHandle = false;

        // --- Drawing Logic ---
        glDisable(GL_DEPTH_TEST);
        m_debugLineShader.use();
        m_debugLineShader.setMat4("projection", projection);
        m_debugLineShader.setMat4("view", originalView);
        glBindVertexArray(m_arrowVAO);

        int directionalLightCounter = 0; // For UI display numbering
        for (int i = 0; i < lights.size(); ++i) {
            if (lights[i].type == 2) { // We only visualize directional lights
                directionalLightCounter++;

                m_debugLineShader.setVec3("lineColor", lights[i].color);

                glm::mat4 conversionMatrix = glm::mat4(
                    -1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );
                glm::vec3 transformedDir = glm::vec3(conversionMatrix * glm::vec4(lights[i].direction, 0.0f));
                glm::vec3 arrowPos = camera.Target - (transformedDir * 50.0f);

                glm::mat4 modelMatrix =
                    glm::translate(glm::mat4(1.0f), arrowPos) *
                    glm::mat4_cast(glm::rotation(glm::vec3(0.0f, 0.0f, -1.0f), glm::normalize(transformedDir))) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(20.0f * lights[i].intensity));

                m_debugLineShader.setMat4("model", modelMatrix);
                glLineWidth(lights[i].intensity * 2.0f + 1.0f);
                glDrawArrays(GL_LINES, 0, 10);

                // --- Interaction Logic ---
                glm::vec4 viewport = glm::vec4(0, 0, screenWidth, screenHeight);
                glm::vec3 screenPos = glm::project(arrowPos, originalView, projection, viewport);

                if (screenPos.z < 1.0f) {
                    ImGui::SetCursorScreenPos(ImVec2(screenPos.x - 16, screenHeight - screenPos.y - 16));
                    ImGui::PushID(i); // Use the absolute and correct index `i`

                    // Create the button that the context menu will attach to
                    ImGui::InvisibleButton("##light_handle", ImVec2(32, 32));

                    // --- Context Menu ---
                    if (ImGui::BeginPopupContextItem("light_context_menu")) {
                        m_interactingLightIndex = i; // Store the correct index

                        ImGui::Text("Arrow #%d", directionalLightCounter);
                        ImGui::Separator();
                        ImGui::DragFloat("Intensity", &lights[i].intensity, 0.01f, 0.0f, 10.0f);
                        ImGui::ColorEdit3("Color", &lights[i].color.r);
                        ImGui::Separator();

                        int directionalLightCount = 0;
                        for (const auto& l : lights) { if (l.type == 2) { directionalLightCount++; } }

                        if (directionalLightCount > 1) {
                            if (ImGui::MenuItem("Delete Light")) {
                                lights.erase(lights.begin() + i); // Use the correct index `i`
                                // Break the loop immediately since we've modified the vector.
                                ImGui::EndPopup();
                                ImGui::PopID();
                                break;
                            }
                        }
                        else {
                            ImGui::TextDisabled("Delete Light");
                            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Cannot delete the last directional light."); }
                        }

                        if (ImGui::MenuItem("Add New Light")) {
                            Light newLight;
                            newLight.type = 2;
                            newLight.direction = glm::vec3(0.0f, 0.0f, -1.0f);
                            newLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
                            newLight.intensity = 0.8f;
                            lights.push_back(newLight);
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::GetForegroundDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 0, 255));

                    // --- Dragging Logic ---
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                        m_interactingLightIndex = i; // Use the correct index `i`

                        ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                        float dragSpeed = 0.005f;

                        bool axisLockActive = false;
                        glm::vec3 rotationAxis;
                        float rotationAngle = 0.0f;

                        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
                            rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                            rotationAngle = mouseDelta.y * dragSpeed;
                            axisLockActive = true;
                        }
                        else if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) {
                            rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                            rotationAngle = -mouseDelta.x * dragSpeed;
                            axisLockActive = true;
                        }
                        else if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
                            rotationAxis = glm::vec3(0.0f, 0.0f, 1.0f);
                            rotationAngle = -mouseDelta.x * dragSpeed;
                            axisLockActive = true;
                        }

                        if (axisLockActive) {
                            glm::quat rotation = glm::angleAxis(rotationAngle, rotationAxis);
                            lights[i].direction = glm::normalize(rotation * lights[i].direction);
                        }
                        else {
                            glm::vec3 transformedDir = glm::vec3(conversionMatrix * glm::vec4(lights[i].direction, 0.0f));
                            glm::quat rotY = glm::angleAxis(mouseDelta.x * dragSpeed, camera.Up);
                            glm::quat rotX = glm::angleAxis(mouseDelta.y * dragSpeed, camera.Right);
                            glm::vec3 newTransformedDir = glm::normalize((rotY * rotX) * transformedDir);
                            lights[i].direction = glm::normalize(glm::vec3(conversionMatrix * glm::vec4(newTransformedDir, 0.0f)));
                        }
                    }

                    ImGui::PopID();
                }
            }
        }

        glBindVertexArray(0);
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);

        // --- NEW: Logging Logic ---
        // If a right click happened but it wasn't on any of the handles, log it.
        if (rightClickHappened && !clickWasOnHandle) {
            std::cout << "Right click detected | Not on Light Interaction Box" << std::endl;
        }

        ImGui::End(); // End the overlay window
    }

    // NEW: At the end of the function, update the last state for the next frame
    m_visualizeLights_lastState = m_visualizeLights;

    // --- END OF MODIFIED VISUALIZATION LOGIC ---

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

    // Calculate the SHA256 hash of the raw NIF data
    std::array<unsigned char, 32> hash_bytes = sha256(nifData.data(), nifData.size());

    // Convert the binary hash to a human-readable hex string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto& byte : hash_bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    currentNifHash = ss.str();

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
            camera.SetInitialState(camera.Target, camera.Radius, camera.Yaw, camera.Pitch);
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
            m_mugshotFrameHeight = frameHeight;
            float frameCenterY = (frameTop_Yup + frameBottom_Yup) / 2.0f;

            // 4. Calculate required camera distance based on the vertical frame ONLY
            const float fovYRadians = glm::radians(m_cameraFovY);
            float distanceForHeight = (m_mugshotFrameHeight / 2.0f) / tan(fovYRadians / 2.0f);
            // 5. Set camera properties
            camera.Radius = distanceForHeight;
            camera.Target = glm::vec3(headCenterX_Yup, frameCenterY, headCenterZ_Yup);
            camera.Yaw = 90.0f; // Use 90 for a direct front-on view
            camera.Pitch = 0.0f;
            camera.updateCameraVectors();

            // Save the calculated position as the new "zero"
            camera.SetInitialState(camera.Target, camera.Radius, camera.Yaw, camera.Pitch);

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
    metadata["nif_sha256"] = currentNifHash;

    metadata["data_folders"] = dataFolders;

    // Add the background color
    metadata["background_color"] = { backgroundColor.r, backgroundColor.g, backgroundColor.b };

    // Add the lighting profile. We parse the stored string into a nested JSON object
    // for cleaner metadata. The 'false' parameter prevents throwing an exception on error.
    nlohmann::json lightingJson = nlohmann::json::parse(lightingProfileJsonString, nullptr, false);
    if (lightingJson.is_discarded()) {
        // If parsing fails, store the raw string as a fallback
        metadata["lighting_profile"] = lightingProfileJsonString;
    }
    else {
        metadata["lighting_profile"] = lightingJson;
    }
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

    // --- 4b. Add the pHYs chunk for DPI metadata ---
    // We'll set 72 DPI to match Natural Lighting Mugshots (for EasyNPC). The unit for the pHYs chunk is pixels per meter.
    // Conversion: pixels_per_meter = dots_per_inch * inches_per_meter
    const double inches_per_meter = 39.3701;
    const int dpi = 72; // Changed from 96 to 72
    unsigned int pixels_per_meter = static_cast<unsigned int>(dpi * inches_per_meter + 0.5); // This will calculate to ~2835

    state.info_png.phys_defined = 1;
    state.info_png.phys_x = pixels_per_meter;
    state.info_png.phys_y = pixels_per_meter;
    state.info_png.phys_unit = 1; // Unit is meters

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

        // Load lighting settings
        lightingProfilePath = data.value("lighting_profile_path", "lighting.json");
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

        data["lighting_profile_path"] = lightingProfilePath;

        std::ofstream o(configPath);
        o << std::setw(4) << data << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: Could not save config file to " << configPath << ": " << e.what() << std::endl;
    }
}

// ============================ NEW TryParse METHOD ============================
bool Renderer::TryParseLightingJson(const std::string& jsonString, std::vector<Light>& outLights) const {
    // Ensure the output vector is clean before starting.
    outLights.clear();
    try {
        nlohmann::json data = nlohmann::json::parse(jsonString);
        if (data.contains("lights") && data["lights"].is_array()) {
            for (const auto& item : data["lights"]) {
                Light light;
                std::string type = item.value("type", "");
                if (type == "ambient") light.type = 1;
                else if (type == "directional") light.type = 2;
                else continue;

                if (item.contains("direction")) {
                    light.direction = glm::normalize(glm::vec3(item["direction"][0], item["direction"][1], item["direction"][2]));
                }
                if (item.contains("color")) {
                    light.color = { item["color"][0], item["color"][1], item["color"][2] };
                }
                light.intensity = item.value("intensity", 1.0f);
                outLights.push_back(light);
            }
        }
        return true; // Success (even if no lights are present)
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to parse lighting profile JSON: " << e.what() << std::endl;
        outLights.clear(); // Ensure output is empty on failure.
        return false; // Failure
    }
}

// ============================ REFACTORED PUBLIC METHOD ============================
void Renderer::setLightingProfileFromJsonString(const std::string& jsonString) {
    std::cout << "--- Loading lighting profile from direct JSON string ---" << std::endl;

    std::vector<Light> parsedLights;
    // Attempt to parse the provided JSON string.
    if (TryParseLightingJson(jsonString, parsedLights)) {
        // On success, update the renderer's state.
        this->lights = parsedLights;
        this->lightingProfileJsonString = jsonString;
    }
    else {
        // If it fails, print a warning and fall back to the profile from the config file.
        std::cerr << "Warning: Invalid JSON string provided. Falling back to lighting profile from settings file." << std::endl;
        loadLightingProfile(lightingProfilePath);
    }
}


// ============================ REFACTORED ORIGINAL METHOD ============================
void Renderer::loadLightingProfile(const std::string& path) {
    std::string jsonContent;

    if (path.empty() || !std::filesystem::exists(path)) {
        std::cout << "Did not find lighting profile at \"" << path << "\". Using default." << std::endl;
        nlohmann::json defaultJson;
        defaultJson["lights"] = nlohmann::json::array({
            {
                {"type", "directional"},
                {"direction", {0.5f, 0.5f, 1.0f}},
                {"color", {1.0f, 1.0f, 1.0f}},
                {"intensity", 1.0f}
            },
            {
                {"type", "ambient"},
                {"color", {0.15f, 0.15f, 0.15f}},
                {"intensity", 1.0f}
            }
            });
        jsonContent = defaultJson.dump(4);
    }
    else {
        try {
            std::cout << "--- Loading lighting profile from: " << path << " ---" << std::endl;
            std::ifstream f(path);
            std::stringstream buffer;
            buffer << f.rdbuf();
            jsonContent = buffer.str();
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load lighting profile file: " << e.what() << std::endl;
            jsonContent = "{}"; // Use empty JSON on file read error 
        }
    }

    // Now, parse the final jsonContent string, regardless of its source.
    std::vector<Light> parsedLights;
    if (TryParseLightingJson(jsonContent, parsedLights)) {
        this->lights = parsedLights;
        this->lightingProfileJsonString = jsonContent;
    }
    else {
        // This fallback handles corrupt files or invalid default JSON.
        this->lights.clear();
        this->lightingProfileJsonString = "{}";
    }
}

void Renderer::saveLightingProfile(const std::string& path) {
    if (path.empty()) {
        std::cerr << "Cannot save lighting profile: no path specified." << std::endl;
        return;
    }

    nlohmann::json data;
    data["lights"] = nlohmann::json::array();
    for (const auto& light : lights) {
        nlohmann::json lightJson;
        if (light.type == 1) lightJson["type"] = "ambient";
        if (light.type == 2) lightJson["type"] = "directional";

        lightJson["color"] = { light.color.r, light.color.g, light.color.b };
        lightJson["intensity"] = light.intensity;
        if (light.type == 2) {
            lightJson["direction"] = { light.direction.x, light.direction.y, light.direction.z };
        }
        data["lights"].push_back(lightJson);
    }

    try {
        std::ofstream o(path);
        o << std::setw(4) << data << std::endl;
        std::cout << "Lighting profile saved to \"" << path << "\"." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving lighting profile: " << e.what() << std::endl;
    }
}

// --- NEW PUBLIC HANDLER IMPLEMENTATIONS ---
void Renderer::HandleMouseButton(int button, int action, int mods) {
    bool captureMouse = ImGui::GetIO().WantCaptureMouse;
    if (m_visualizeLights) {
        // We now ONLY check if an item is hovered.
        captureMouse = ImGui::IsAnyItemHovered();
    }
    if (captureMouse) {
        isRotating = false;
        isPanning = false;
        return;
    }

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
    bool captureMouse = ImGui::GetIO().WantCaptureMouse;
    if (m_visualizeLights) {
        // We now ONLY check if an item is hovered.
        captureMouse = ImGui::IsAnyItemHovered();
    }
    if (captureMouse) {
        isRotating = false;
        isPanning = false;
        return;
    }

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
    bool captureMouse = ImGui::GetIO().WantCaptureMouse;
    if (m_visualizeLights) {
        // We now ONLY check if an item is hovered.
        captureMouse = ImGui::IsAnyItemHovered();
    }
    if (captureMouse) return;

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

