// main.cpp

#include <glad/glad.h> 
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cxxopts.hpp>
#include <filesystem>
#include <combaseapi.h> // For CoInitializeEx

void PrintVersion() {
    std::cout << PROGRAM_VERSION << std::endl;
}

int main(int argc, char** argv) {
    // Set the main thread to be a Single-Threaded Apartment.
    // This is REQUIRED for WinRT UI components like the File Picker.
    // It must be the first thing you do.
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    cxxopts::Options options("NPC Portrait Creator", "NIF file renderer and thumbnail generator");
    options.add_options()
        ("f,file", "Input .nif file", cxxopts::value<std::string>())
        ("o,output", "Output .png file", cxxopts::value<std::string>())
        ("d,data", "A data directory. Can be specified multiple times.", cxxopts::value<std::vector<std::string>>())
        ("g,gamedata", "Sets the base game data directory (lowest priority).", cxxopts::value<std::string>())
        ("s,skeleton", "Path to a custom skeleton.nif file", cxxopts::value<std::string>())
        ("headless", "Run in headless mode without a visible window")
        // Camera absolute position controls
        ("camX", "Camera X position (disables auto-framing)", cxxopts::value<float>()->default_value("0"))
        ("camY", "Camera Y position (disables auto-framing)", cxxopts::value<float>()->default_value("0"))
        ("camZ", "Camera Z position (disables auto-framing)", cxxopts::value<float>()->default_value("0"))
        ("pitch", "Camera pitch angle (works in both modes)", cxxopts::value<float>()->default_value("0"))
        ("yaw", "Camera yaw angle (works in both modes)", cxxopts::value<float>()->default_value("0"))
        ("roll", "Camera roll angle (works in both modes)", cxxopts::value<float>()->default_value("0"))
        // Camera relative (mugshot) controls
        ("head-top-offset", "Top margin for head as a percentage (e.g., 0.15 for 15%)", cxxopts::value<float>())
        ("head-bottom-offset", "Bottom margin for head as a percentage (e.g., -0.02 for -2%)", cxxopts::value<float>())
        // Lighting profile
        ("lighting", "Path to a custom lighting profile JSON file", cxxopts::value<std::string>())
        ("lighting-json", "A JSON string defining a custom lighting profile (overrides --lighting)", cxxopts::value<std::string>())
        // New image resolution controls
        ("imgX", "Horizontal resolution of the output PNG", cxxopts::value<int>())
        ("imgY", "Vertical resolution of the output PNG", cxxopts::value<int>())
        ("bgcolor", "Background R,G,B color (e.g. \"0.1,0.5,1.0\")", cxxopts::value<std::string>())
        ("fov", "Camera vertical Field of View in degrees", cxxopts::value<float>())
        ("mipmap", "Enable texture mipmapping (default: off for max quality)", cxxopts::value<std::string>())
        ("lod-bias", "Texture LOD bias (negative = sharper, e.g., -2.0)", cxxopts::value<float>())
        ("normal-hack", "Enable normal map hack: true or false (default: true)", cxxopts::value<std::string>())
        ("v,version", "Print the program version and exit")
        ("h,help", "Print usage");
    auto result = options.parse(argc, argv);

    if (result.count("version")) {
        PrintVersion();
        return 0;
    }

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::cout << "--- [Debug] Parsing Command-Line Arguments ---" << std::endl;

    bool isHeadless = result.count("headless") > 0;
    std::cout << "[headless] " << (isHeadless ? "Found" : "Not found") << std::endl;

    try {
        std::filesystem::path exePath(argv[0]);
        std::filesystem::path exeDir = exePath.parent_path();
        Renderer renderer(1280, 720, exeDir.string());

        // 1. Load settings from config file first
        renderer.loadConfig();

        // 2. Override with any command-line arguments
        if (result.count("gamedata")) {
            std::string value = result["gamedata"].as<std::string>();
            std::cout << "[gamedata] Found: \"" << value << "\"" << std::endl;
            renderer.setGameDataDirectory(value);
        }
        else {
            std::cout << "[gamedata] Not found" << std::endl;
        }

        if (result.count("data")) {
            auto values = result["data"].as<std::vector<std::string>>();
            std::cout << "[data] Found " << values.size() << " directories:" << std::endl;
            for (const auto& dir : values) {
                std::cout << "  - \"" << dir << "\"" << std::endl;
            }
            renderer.setDataFolders(values);
        }
        else {
            std::cout << "[data] Not found" << std::endl;
        }

        if (result.count("skeleton")) {
            std::string value = result["skeleton"].as<std::string>();
            std::cout << "[skeleton] Found: \"" << value << "\"" << std::endl;
            renderer.loadCustomSkeleton(value);
        }
        else {
            std::cout << "[skeleton] Not found" << std::endl;
        }

        if (result.count("head-top-offset")) {
            float value = result["head-top-offset"].as<float>();
            std::cout << "[head-top-offset] Found: " << value << std::endl;
            renderer.setMugshotTopOffset(value);
        }
        else {
            std::cout << "[head-top-offset] Not found" << std::endl;
        }

        if (result.count("head-bottom-offset")) {
            float value = result["head-bottom-offset"].as<float>();
            std::cout << "[head-bottom-offset] Found: " << value << std::endl;
            renderer.setMugshotBottomOffset(value);
        }
        else {
            std::cout << "[head-bottom-offset] Not found" << std::endl;
        }

        if (result.count("imgX")) {
            int value = result["imgX"].as<int>();
            std::cout << "[imgX] Found: " << value << std::endl;
            renderer.setImageResolutionX(value);
        }
        else {
            std::cout << "[imgX] Not found" << std::endl;
        }

        if (result.count("imgY")) {
            int value = result["imgY"].as<int>();
            std::cout << "[imgY] Found: " << value << std::endl;
            renderer.setImageResolutionY(value);
        }
        else {
            std::cout << "[imgY] Not found" << std::endl;
        }

        if (result.count("fov")) {
            float value = result["fov"].as<float>();
            std::cout << "[fov] Found: " << value << std::endl;
            renderer.setFov(value);
        }
        else {
            std::cout << "[fov] Not found" << std::endl;
        }

        if (result.count("mipmap")) {
            std::string valueStr = result["mipmap"].as<std::string>();
            bool value = (valueStr == "true" || valueStr == "1" || valueStr == "yes");
            std::cout << "[mipmap] Found: raw=\"" << valueStr << "\", parsed=" << (value ? "true" : "false") << std::endl;
            renderer.setTextureMipmapping(value);
        }
        else {
            std::cout << "[mipmap] Not found" << std::endl;
        }

        if (result.count("lod-bias")) {
            float value = result["lod-bias"].as<float>();
            std::cout << "[lod-bias] Found: " << value << std::endl;
            renderer.setTextureLodBias(value);
        }
        else {
            std::cout << "[lod-bias] Not found" << std::endl;
        }

        if (result.count("normal-hack")) {
            std::string valueStr = result["normal-hack"].as<std::string>();
            bool value = (valueStr == "true" || valueStr == "1" || valueStr == "yes");
            std::cout << "[normal-hack] Found: raw=\"" << valueStr << "\", parsed=" << (value ? "true" : "false") << std::endl;
            renderer.setNormalMapHack(value);
        }
        else {
            std::cout << "[normal-hack] Not found (using default: true)" << std::endl;
        }

        // Always override camera if specified on command line
        // Position parameters trigger absolute camera mode
        if (result.count("camX") || result.count("camY") || result.count("camZ")) {
            float camX = result["camX"].as<float>();
            float camY = result["camY"].as<float>();
            float camZ = result["camZ"].as<float>();
            float pitch = result["pitch"].as<float>();
            float yaw = result["yaw"].as<float>();
            float roll = result["roll"].as<float>();

            std::cout << "[camera-absolute] Parsed: camX=" << camX << ", camY=" << camY
                << ", camZ=" << camZ << ", pitch=" << pitch << ", yaw=" << yaw
                << ", roll=" << roll << std::endl;

            renderer.setAbsoluteCamera(camX, camY, camZ, pitch, yaw, roll);
        }
        else {
            std::cout << "[camera-absolute] Not specified (using auto-framing)" << std::endl;
        }

        // Euler angles work in both modes - just store them
        if (result.count("pitch") || result.count("yaw") || result.count("roll")) {
            float pitch = result["pitch"].as<float>();
            float yaw = result["yaw"].as<float>();
            float roll = result["roll"].as<float>();

            std::cout << "[euler-angles] Parsed: pitch=" << pitch << ", yaw=" << yaw
                << ", roll=" << roll << std::endl;

            renderer.setEulerAngles(pitch, yaw, roll);
        }
        else {
            std::cout << "[euler-angles] Not specified" << std::endl;
        }

        if (result.count("bgcolor")) {
            std::string bgColorStr = result["bgcolor"].as<std::string>();
            std::cout << "[bgcolor] Raw value: \"" << bgColorStr << "\"" << std::endl;

            std::stringstream ss(bgColorStr);
            std::string segment;
            std::vector<float> colorComponents;
            while (std::getline(ss, segment, ',')) {
                try {
                    colorComponents.push_back(std::stof(segment));
                }
                catch (const std::invalid_argument&) {
                    colorComponents.clear(); // Invalidate on error
                    break;
                }
            }
            if (colorComponents.size() == 3) {
                std::cout << "[bgcolor] Parsed: R=" << colorComponents[0] << ", G="
                    << colorComponents[1] << ", B=" << colorComponents[2] << std::endl;
                renderer.setBackgroundColor({ colorComponents[0], colorComponents[1], colorComponents[2] });
            }
            else {
                std::cout << "[bgcolor] Parse failed - invalid format" << std::endl;
                std::cerr << "Warning: Invalid --bgcolor format. Use R,G,B values from 0.0 to 1.0 (e.g., \"0.1,0.2,0.3\")." << std::endl;
            }
        }
        else {
            std::cout << "[bgcolor] Not found" << std::endl;
        }

        // Lighting profile logic: --lighting-json takes precedence over --lighting
        if (result.count("lighting-json")) {
            std::string value = result["lighting-json"].as<std::string>();
            std::cout << "[lighting-json] Found: \"" << value << "\"" << std::endl;
            renderer.setLightingProfileFromJsonString(value);
        }
        else if (result.count("lighting")) {
            std::string value = result["lighting"].as<std::string>();
            std::cout << "[lighting] Found: \"" << value << "\"" << std::endl;
            renderer.setLightingProfile(value);
        }
        else {
            std::cout << "[lighting/lighting-json] Not found" << std::endl;
        }

        renderer.init(isHeadless);

        if (isHeadless) {
            if (!result.count("file") || !result.count("output")) {
                std::cerr << "Error: In headless mode, --file and --output are required." << std::endl;
                return 1;
            }
            std::string nifPath = result["file"].as<std::string>();
            std::string outputPath = result["output"].as<std::string>();

            std::cout << "\n--- [Headless Mode] Processing ---" << std::endl;
            std::cout << "[file] \"" << nifPath << "\"" << std::endl;
            std::cout << "[output] \"" << outputPath << "\"" << std::endl;

            renderer.loadNifModel(nifPath);

            // --- START MODIFICATION: WARM-UP LOOP ---
            // Run a few frames to allow the OpenGL context to stabilize.
            std::cout << "--- [Debug] Running 5 warm-up frames... ---" << std::endl;
            for (int i = 0; i < 5; ++i) {
                renderer.renderFrame();
                glfwSwapBuffers(renderer.getWindow());
                glfwPollEvents();
            }
            // --- END MODIFICATION ---

            // Now perform the final, definitive render and save.
            std::cout << "--- [Debug] Warm-up complete. Capturing final frame... ---" << std::endl;
            renderer.renderFrame();         // Draw the model to the back buffer.
            renderer.saveToPNG(outputPath); // Save the contents of the back buffer.

            std::cout << "Image saved to " << outputPath << std::endl;
        }
        else {
            renderer.run();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}