// main.cpp

#include <glad/glad.h> 
#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cxxopts.hpp>
#include <filesystem>

int main(int argc, char** argv) {
    cxxopts::Options options("Mugshotter", "NIF file renderer and thumbnail generator");
    options.add_options()
        ("f,file", "Input .nif file", cxxopts::value<std::string>())
        ("o,output", "Output .png file", cxxopts::value<std::string>())
        ("r,root", "Set the fallback root data directory", cxxopts::value<std::string>())
        ("s,skeleton", "Path to a custom skeleton.nif file", cxxopts::value<std::string>()) // New option
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
        // Get the directory where the executable is running
        std::filesystem::path exePath(argv[0]);
        std::filesystem::path exeDir = exePath.parent_path();

        // Pass this path to the Renderer's constructor
        Renderer renderer(1280, 720, exeDir.string());
        if (result.count("root")) {
            renderer.setFallbackRootDirectory(result["root"].as<std::string>());
        }

        renderer.init(isHeadless);

        // Load custom skeleton if provided
        if (result.count("skeleton")) {
            renderer.loadCustomSkeleton(result["skeleton"].as<std::string>());
        }

        if (isHeadless) {
            std::cout << "DEBUG: Running in HEADLESS mode." << std::endl;
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
        else {
            std::cout << "DEBUG: Running in GUI mode." << std::endl;
            renderer.run();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "DEBUG: Main function is about to exit. Press Enter to close." << std::endl;
    std::cin.get(); // This will pause the console

    return 0;
}