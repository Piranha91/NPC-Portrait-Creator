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
        ("camX", "Camera X position", cxxopts::value<float>()->default_value("0"))
        ("camY", "Camera Y position", cxxopts::value<float>()->default_value("0"))
        ("camZ", "Camera Z position", cxxopts::value<float>()->default_value("0"))
        ("pitch", "Camera pitch angle", cxxopts::value<float>()->default_value("0"))
        ("yaw", "Camera yaw angle", cxxopts::value<float>()->default_value("0"))
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

    bool isHeadless = result.count("headless") > 0;
    try {
        std::filesystem::path exePath(argv[0]);
        std::filesystem::path exeDir = exePath.parent_path();
        Renderer renderer(1280, 720, exeDir.string());

        // 1. Load settings from config file first
        renderer.loadConfig();

        // 2. Override with any command-line arguments
        if (result.count("gamedata")) {
            renderer.setGameDataDirectory(result["gamedata"].as<std::string>());
        }
        if (result.count("data")) {
            renderer.setDataFolders(result["data"].as<std::vector<std::string>>());
        }
        if (result.count("skeleton")) {
            renderer.loadCustomSkeleton(result["skeleton"].as<std::string>());
        }
        if (result.count("head-top-offset")) {
            renderer.setMugshotTopOffset(result["head-top-offset"].as<float>());
        }
        if (result.count("head-bottom-offset")) {
            renderer.setMugshotBottomOffset(result["head-bottom-offset"].as<float>());
        }
        if (result.count("imgX")) {
            renderer.setImageResolutionX(result["imgX"].as<int>());
        }
        if (result.count("imgY")) {
            renderer.setImageResolutionY(result["imgY"].as<int>());
        }
        if (result.count("fov")) {
            renderer.setFov(result["fov"].as<float>());
        }

        // Always override camera if specified on command line
        if (result.count("camX") || result.count("camY") || result.count("camZ") || result.count("pitch") || result.count("yaw")) {
            renderer.setAbsoluteCamera(
                result["camX"].as<float>(), result["camY"].as<float>(), result["camZ"].as<float>(),
                result["pitch"].as<float>(), result["yaw"].as<float>()
            );
        }

        if (result.count("bgcolor")) {
            std::string bgColorStr = result["bgcolor"].as<std::string>();
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
                renderer.setBackgroundColor({ colorComponents[0], colorComponents[1], colorComponents[2] });
            }
            else {
                std::cerr << "Warning: Invalid --bgcolor format. Use R,G,B values from 0.0 to 1.0 (e.g., \"0.1,0.2,0.3\")." << std::endl;
            }
        }

        // Lighting profile logic: --lighting-json takes precedence over --lighting
        if (result.count("lighting-json")) {
            renderer.setLightingProfileFromJsonString(result["lighting-json"].as<std::string>());
        }
        else if (result.count("lighting")) {
            renderer.setLightingProfile(result["lighting"].as<std::string>());
        }

        renderer.init(isHeadless);

        if (isHeadless) {
            if (!result.count("file") || !result.count("output")) {
                std::cerr << "Error: In headless mode, --file and --output are required." << std::endl;
                return 1;
            }
            std::string nifPath = result["file"].as<std::string>();
            std::string outputPath = result["output"].as<std::string>();

            std::cout << "Running in headless mode..." << std::endl;

            std::cout << "--- [Headless Arg] Parsing Command-Line Arguments ---" << std::endl;
            if (result.count("file")) {
                std::cout << "  [Parsed] --file: " << result["file"].as<std::string>() << std::endl;
            }
            if (result.count("output")) {
                std::cout << "  [Parsed] --output: " << result["output"].as<std::string>() << std::endl;
            }
            if (result.count("gamedata")) {
                std::cout << "  [Parsed] --gamedata: " << result["gamedata"].as<std::string>() << std::endl;
            }
            else {
                std::cout << "  [Default] --gamedata: Not provided." << std::endl;
            }
            if (result.count("data")) {
                for (const auto& dir : result["data"].as<std::vector<std::string>>()) {
                    std::cout << "  [Parsed] --data: " << dir << std::endl;
                }
            }
            else {
                std::cout << "  [Default] --data: Not provided." << std::endl;
            }
            if (result.count("skeleton")) {
                std::cout << "  [Parsed] --skeleton: " << result["skeleton"].as<std::string>() << std::endl;
            }
            else {
                std::cout << "  [Default] --skeleton: Not provided." << std::endl;
            }
            std::cout << "  [Parsed] --camX: " << result["camX"].as<float>() << std::endl;
            std::cout << "  [Parsed] --camY: " << result["camY"].as<float>() << std::endl;
            std::cout << "  [Parsed] --camZ: " << result["camZ"].as<float>() << std::endl;
            std::cout << "  [Parsed] --pitch: " << result["pitch"].as<float>() << std::endl;
            std::cout << "  [Parsed] --yaw: " << result["yaw"].as<float>() << std::endl;
            if (result.count("head-top-offset")) {
                std::cout << "  [Parsed] --head-top-offset: " << result["head-top-offset"].as<float>() << std::endl;
            }
            else {
                std::cout << "  [Default] --head-top-offset: Not provided, using config value." << std::endl;
            }
            if (result.count("head-bottom-offset")) {
                std::cout << "  [Parsed] --head-bottom-offset: " << result["head-bottom-offset"].as<float>() << std::endl;
            }
            else {
                std::cout << "  [Default] --head-bottom-offset: Not provided, using config value." << std::endl;
            }
            if (result.count("lighting-json")) {
                std::string jsonStr = result["lighting-json"].as<std::string>();
                // Call the new public method to check validity. Note that this does NOT
                // alter the renderer's state. We create a dummy vector for the output.
                std::vector<Light> dummyLights;
                bool isValid = renderer.TryParseLightingJson(jsonStr, dummyLights);

                std::cout << "  [Parsed] --lighting-json: " << jsonStr << (isValid ? " (Valid JSON)" : " (INVALID JSON)") << std::endl;
            }
            else if (result.count("lighting")) {
                std::cout << "  [Parsed] --lighting: " << result["lighting"].as<std::string>() << std::endl;
            }
            else {
                std::cout << "  [Default] --lighting: Not provided, using default profile." << std::endl;
            }
            if (result.count("imgX")) {
                std::cout << "  [Parsed] --imgX: " << result["imgX"].as<int>() << std::endl;
            }
            else {
                std::cout << "  [Default] --imgX: Not provided, using config value." << std::endl;
            }
            if (result.count("imgY")) {
                std::cout << "  [Parsed] --imgY: " << result["imgY"].as<int>() << std::endl;
            }
            else {
                std::cout << "  [Default] --imgY: Not provided, using config value." << std::endl;
            }
            if (result.count("bgcolor")) {
                std::string bgColorStr = result["bgcolor"].as<std::string>();
                std::stringstream ss(bgColorStr);
                std::string segment;
                std::vector<float> colorComponents;
                while (std::getline(ss, segment, ',')) {
                    try {
                        colorComponents.push_back(std::stof(segment));
                    }
                    catch (const std::exception&) {
                        colorComponents.clear();
                        break;
                    }
                }
                if (colorComponents.size() == 3) {
                    std::cout << "  [Parsed] --bgcolor: " << bgColorStr << " (Valid format)" << std::endl;
                }
                else {
                    std::cout << "  [Invalid] --bgcolor: \"" << bgColorStr << "\" (Invalid format. Expected R,G,B floats)" << std::endl;
                }
            }
            else {
                std::cout << "  [Default] --bgcolor: Not provided, using config value." << std::endl;
            }

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