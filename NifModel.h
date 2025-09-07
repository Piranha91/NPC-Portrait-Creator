#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>
#include <memory>

#include <NifFile.hpp>

// Forward-declare Shader so we can use it in the draw function signature
class Shader;

// The MeshShape struct should be fully defined here so the compiler
// knows about its members (VAO, VBO, etc.)
struct MeshShape {
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLsizei indexCount = 0;
    glm::mat4 transform;

    void draw() const;
    void cleanup();
};


class NifModel {
public:
    NifModel();
    ~NifModel();

    bool load(const std::string& path);
    void draw(Shader& shader);
    void cleanup();

    // Getter for texture paths found in the NIF
    std::vector<std::string> getTextures() const;

private:
    // Declare the nifly::NifFile object and the vector of shapes as member variables
    // so they persist outside of the load() function.
    nifly::NifFile nif;
    std::vector<MeshShape> shapes;
    std::vector<std::string> texturePaths; // NEW: Stores texture paths
};