#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <vector>
#include <algorithm> 
#include <filesystem> // NEW: Include for checking file paths

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// --- ImGui Includes ---
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// --- File Dialog Includes ---
#include "tinyfiledialogs.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

Renderer::Renderer(int width, int height)
    : screenWidth(width), screenHeight(height),
    camera(glm::vec3(0.0f, 50.0f, 300.0f)), lastX(width / 2.0f), lastY(height / 2.0f),
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
    const char* glsl_version = "#version 330";
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
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, screenWidth, screenHeight);
    shader.load("shaders/basic.vert", "shaders/basic.frag");

    model = std::make_unique<NifModel>();

    loadConfig();

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

void Renderer::initUI() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Renderer::run() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        processInput();

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
        ImGui::EndMainMenuBar();
    }
}

void Renderer::shutdownUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Renderer::renderFrame() {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader.use();
    glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)screenWidth / (float)screenHeight, 0.1f, 10000.0f);
    glm::mat4 view = camera.GetViewMatrix();

    shader.setMat4("projection", projection);
    shader.setMat4("view", view);

    if (model) {
        model->draw(shader);
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
        std::cout << "Auto-detected root directory: " << rootDirectory << std::endl;
    }
    else {
        rootDirectory = fallbackRootDirectory;
        std::cout << "Could not auto-detect root. Using fallback: " << rootDirectory << std::endl;
    }

    if (!model) {
        model = std::make_unique<NifModel>();
    }
    if (model->load(path)) {
        currentNifPath = path;
        saveConfig();
        textureManager.setActiveDirectories(rootDirectory, fallbackRootDirectory);
        std::cout << "\n--- Checking Textures for " << path << " ---\n";
        const auto& textures = model->getTextures();
        if (textures.empty()) {
            std::cout << "No textures found in this NIF file.\n";
        }
        else {
            for (const auto& texPath : textures) {
                textureManager.checkTexture(texPath);
            }
        }
        std::cout << "---------------------------------------------------------\n" << std::endl;
    }
    else {
        std::cerr << "Renderer failed to load NIF model." << std::endl;
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

void Renderer::processInput() {
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, 0.5f);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, 0.5f);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, 0.5f);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, 0.5f);
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