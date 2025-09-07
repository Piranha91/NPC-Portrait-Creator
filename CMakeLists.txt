# CMakeList.txt for Mugshotter
cmake_minimum_required(VERSION 3.20)

# --- Project Definition ---
# Corrected to include both CXX (C++) and C languages for the project
project(Mugshotter LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Project configured for NIF rendering with UI.")

# --- vcpkg Integration ---
# This automatically finds the vcpkg toolchain file
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# --- Find Packages (Dependencies) ---
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nifly REQUIRED)
find_package(gli REQUIRED)
find_package(cxxopts REQUIRED)
find_package(imgui REQUIRED CONFIG) # Use CONFIG mode for ImGui

# --- Executable Definition ---
add_executable(Mugshotter
    main.cpp
    Renderer.h
    Renderer.cpp
    Shader.h
    Shader.cpp
    Camera.h
    Camera.cpp
    NifModel.h
    NifModel.cpp
    vendor/tinyfiledialogs/tinyfiledialogs.c
)

# --- Include Directories ---
target_include_directories(Mugshotter PRIVATE
    "${PROJECT_SOURCE_DIR}/vendor"
    "${PROJECT_SOURCE_DIR}/vendor/glad/include"
    "${PROJECT_SOURCE_DIR}/vendor/tinyfiledialogs"
    ${nifly_INCLUDE_DIRS}
    ${gli_INCLUDE_DIRS}
    ${cxxopts_INCLUDE_DIRS}
)

# --- Link Libraries ---
target_link_libraries(Mugshotter PRIVATE
    glfw
    OpenGL::GL
    glm::glm
    nifly       # Use plain name as corrected before
    gli         # Corrected from gli::gli to plain name
    imgui::imgui       # Corrected from imgui::imgui to plain name
    cxxopts::cxxopts
)

# --- Source file for GLAD ---
# Corrected the path to be absolute to the project source directory
add_library(glad STATIC "${PROJECT_SOURCE_DIR}/vendor/glad/src/glad.c")
target_include_directories(glad PRIVATE "${PROJECT_SOURCE_DIR}/vendor/glad/include")
target_link_libraries(Mugshotter PRIVATE glad)

# --- Post-Build Step: Copy Shaders Directory ---
# This command ensures the 'shaders' folder is copied to the same directory
# as the final executable, so the program can find the .vert and .frag files.
add_custom_command(TARGET Mugshotter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders" # Source
    "$<TARGET_FILE_DIR:Mugshotter>/shaders" # Destination
    COMMENT "Copying shaders to build directory"
)