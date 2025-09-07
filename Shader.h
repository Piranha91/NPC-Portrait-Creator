#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glm/glm.hpp>

class Shader {
public:
    unsigned int ID;

    Shader() : ID(0) {}
    // Load shaders from file and build the program
    void load(const char* vertexPath, const char* fragmentPath);
    // Use the shader program
    void use();
    // Utility uniform functions
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;

private:
    void checkCompileErrors(unsigned int shader, std::string type);
};

#endif
