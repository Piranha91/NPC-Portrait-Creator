#include <glad/glad.h> // Must be included before GLFW
#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Command-line argument parsing
#include <cxxopts.hpp>

// --- Main Application Entry Point ---
int main(int argc, char** argv) {
    // --- Argument Parsing ---
    cxxopts::Options options("Mugshotter", "NIF file renderer and thumbnail generator");
    options.add_options()
        ("f,file", "Input .nif file", cxxopts::value<std::string>())
        ("o,output", "Output .png file", cxxopts::value<std::string>())
        ("headless", "Run in headless mode without a visible window")
        ("camX", "Camera X position", cxxopts::value<float>()->default_value("0"))
        ("camY", "Camera Y position", cxxopts::value<float>()->default_value("0"))
        ("camZ", "Camera Z position", cxxopts::value<float>()->default_value("5.0"))
        ("pitch", "Camera pitch angle", cxxopts::value<float>()->default_value("0"))
        ("yaw", "Camera yaw angle", cxxopts::value<float>()->default_value("-90.0"))
        ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool isHeadless = result.count("headless") > 0;
    
    try {
        Renderer renderer(1280, 720);
        renderer.init(isHeadless);

        // --- Headless Mode Execution ---
        if (isHeadless) {
            if (!result.count("file") || !result.count("output")) {
                std::cerr << "Error: In headless mode, --file and --output are required." << std::endl;
                return 1;
            }
            std::string nifPath = result["file"].as<std::string>();
            std::string outputPath = result["output"].as<std::string>();
            
            renderer.setCamera(
                result["camX"].as<float>(), result["camY"].as<float>(), result["camZ"].as<float>(),
                result["pitch"].as<float>(), result["yaw"].as<float>()
            );

            std::cout << "Running in headless mode..." << std::endl;
            renderer.loadNifModel(nifPath);
            renderer.renderFrame();
            renderer.saveToPNG(outputPath);
            std::cout << "Image saved to " << outputPath << std::endl;

        } 
        // --- GUI Mode Execution ---
        else {
            renderer.run();
        }

    } catch (const std::exception& e) {
        std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

