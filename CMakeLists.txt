# CMakeList.txt for Mugshotter
cmake_minimum_required(VERSION 3.20)

project(Mugshotter LANGUAGES CXX C)
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
    BsaManager.h
    BsaManager.cpp
    TextureManager.h
    TextureManager.cpp
    Skeleton.h
    Skeleton.cpp
    vendor/tinyfiledialogs/tinyfiledialogs.c

    # Manually include the libbsarch C++ source files in the build
    vendor/libbsarch/src/bs_archive.cpp
    vendor/libbsarch/src/bs_archive_entries.cpp
    vendor/libbsarch/src/utils/convertible_string.cpp
    vendor/libbsarch/src/utils/string_convert.cpp
    vendor/libbsarch/src/utils/convertible_ostream.cpp
)

target_include_directories(Mugshotter PRIVATE
    "${PROJECT_SOURCE_DIR}/vendor"
    "${PROJECT_SOURCE_DIR}/vendor/glad/include"
    "${PROJECT_SOURCE_DIR}/vendor/tinyfiledialogs"
    "${PROJECT_SOURCE_DIR}/vendor/libbsarch/src"
    ${nifly_INCLUDE_DIRS}
    ${gli_INCLUDE_DIRS}
    ${cxxopts_INCLUDE_DIRS}
)

target_link_libraries(Mugshotter PRIVATE
    glfw
    OpenGL::GL
    glm::glm
    nifly       
    gli         
    imgui::imgui  
    cxxopts::cxxopts
    libbsarch
)

add_library(glad STATIC "${PROJECT_SOURCE_DIR}/vendor/glad/src/glad.c")
target_include_directories(glad PRIVATE "${PROJECT_SOURCE_DIR}/vendor/glad/include")
target_link_libraries(Mugshotter PRIVATE glad)

add_custom_command(TARGET Mugshotter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
    "$<TARGET_FILE_DIR:Mugshotter>/shaders"
    COMMENT "Copying shaders to build directory"
)