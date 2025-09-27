#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Defines directions for keyboard-based rotation
enum class KeyRotation {
    UP,
    DOWN,
    LEFT,
    RIGHT
};

// Default camera values
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SENSITIVITY = 0.25f;
const float PAN_SENSITIVITY = 0.001f;
const float DEFAULT_RADIUS = 300.0f; // Default zoom level

class Camera {
public:
    // Camera Attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    glm::vec3 Target; // The point the camera is looking at

    // Euler Angles
    float Yaw;
    float Pitch;

    // Camera options
    float MouseSensitivity;
    float PanSensitivity;
    float Radius; // Distance from the target

    Camera(glm::vec3 target = glm::vec3(0.0f, 50.0f, 0.0f), float radius = DEFAULT_RADIUS, float yaw = YAW, float pitch = PITCH);

    glm::mat4 GetViewMatrix();
    void Reset();

    // Input processing
    void ProcessMouseOrbit(float xoffset, float yoffset);
    void ProcessMousePan(float xoffset, float yoffset);
    void ProcessMouseScroll(float yoffset);
    void ProcessKeyRotation(KeyRotation direction);
    void updateCameraVectors();

private:
    
};