# CMakeList.txt for NPC Portrait Generator
cmake_minimum_required(VERSION 3.20)

project(NPCPortraitCreator LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# Builds libbsarch from the local vendor/libbsarch directory
add_subdirectory(vendor/libbsarch)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nifly REQUIRED)
find_package(gli REQUIRED)
find_package(cxxopts REQUIRED)
find_package(imgui REQUIRED CONFIG)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(NPCPortraitCreator
    main.cpp
    Renderer.h
    Renderer.cpp
    Shader.h
    Shader.cpp
    Camera.h
    Camera.cpp
    NifModel.h
    NifModel.cpp
    BsaManager.h
    BsaManager.cpp
    TextureManager.h
    TextureManager.cpp
    AssetManager.h
    AssetManager.cpp
    Skeleton.h
    Skeleton.cpp
    Version.h
    vendor/tinyfiledialogs/tinyfiledialogs.c
    vendor/lodepng/lodepng.cpp

    # Manually include the libbsarch C++ source files in the build
    vendor/libbsarch/src/bs_archive.cpp
    vendor/libbsarch/src/bs_archive_entries.cpp
    vendor/libbsarch/src/utils/convertible_string.cpp
    vendor/libbsarch/src/utils/string_convert.cpp
    vendor/libbsarch/src/utils/convertible_ostream.cpp
)

target_include_directories(NPCPortraitCreator PRIVATE
    "${PROJECT_SOURCE_DIR}/vendor"
    "${PROJECT_SOURCE_DIR}/vendor/glad/include"
    "${PROJECT_SOURCE_DIR}/vendor/tinyfiledialogs"
    "${PROJECT_SOURCE_DIR}/vendor/libbsarch/src"
    ${nifly_INCLUDE_DIRS}
    ${gli_INCLUDE_DIRS}
    ${cxxopts_INCLUDE_DIRS}
)

target_link_libraries(NPCPortraitCreator PRIVATE
    glfw
    OpenGL::GL
    glm::glm
    nifly       
    gli         
    imgui::imgui  
    cxxopts::cxxopts
    libbsarch
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
)

add_library(glad STATIC "${PROJECT_SOURCE_DIR}/vendor/glad/src/glad.c")
target_include_directories(glad PRIVATE "${PROJECT_SOURCE_DIR}/vendor/glad/include")
target_link_libraries(NPCPortraitCreator PRIVATE glad)

add_custom_command(TARGET NPCPortraitCreator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
    "$<TARGET_FILE_DIR:NPCPortraitCreator>/shaders"
    COMMENT "Copying shaders to build directory"
)

add_custom_command(TARGET NPCPortraitCreator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/lighting.json" 
    "$<TARGET_FILE_DIR:NPCPortraitCreator>/lighting.json"
    COMMENT "Copying lighting profile to build directory if changed"
)