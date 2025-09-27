#include "Camera.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp> // For printing glm::vec3
#include <iostream>

Camera::Camera(glm::vec3 target, float radius, float yaw, float pitch) :
    Target(target), Radius(radius), Yaw(yaw), Pitch(pitch),
    Front(glm::vec3(0.0f, 0.0f, -1.0f)), WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)),
    MouseSensitivity(SENSITIVITY),
    PanSensitivity(PAN_SENSITIVITY) {
    updateCameraVectors();
}

glm::mat4 Camera::GetViewMatrix() {
    return glm::lookAt(Position, Target, Up);
}

void Camera::Reset() {
    Target = glm::vec3(0.0f, 50.0f, 0.0f);
    Radius = DEFAULT_RADIUS;
    Yaw = YAW;
    Pitch = PITCH;
    updateCameraVectors();
}

void Camera::ProcessMouseOrbit(float xoffset, float yoffset) {
    Yaw += xoffset * MouseSensitivity;
    Pitch += yoffset * MouseSensitivity;

    // Constrain pitch
    if (Pitch > 89.0f) Pitch = 89.0f;
    if (Pitch < -89.0f) Pitch = -89.0f;

    updateCameraVectors();
}

void Camera::ProcessMousePan(float xoffset, float yoffset) {
    // --- TEMPORARY DEBUGGING BLOCK ---
    std::cout << "--- Panning ---" << std::endl;
    std::cout << "Mouse Input (x, y): " << xoffset << ", " << yoffset << std::endl;
    std::cout << "Camera Right Vector: " << glm::to_string(Right) << std::endl;
    std::cout << "Camera Up Vector:    " << glm::to_string(Up) << std::endl;
    std::cout << "-----------------" << std::endl;
    // --- END DEBUGGING ---

    float panSpeed = PanSensitivity * Radius;
    Target -= Right * (xoffset * panSpeed);
    Target += Up * (yoffset * panSpeed);
    updateCameraVectors();
}

void Camera::ProcessMouseScroll(float yoffset) {
    // Adjust radius, scaling zoom speed
    Radius -= yoffset * (Radius * 0.1f);
    if (Radius < 1.0f) Radius = 1.0f; // Prevent zooming too close
    updateCameraVectors();
}

void Camera::ProcessKeyRotation(KeyRotation direction) {
    if (direction == KeyRotation::LEFT) Yaw -= 90.0f;
    if (direction == KeyRotation::RIGHT) Yaw += 90.0f;
    if (direction == KeyRotation::UP) Pitch += 90.0f;
    if (direction == KeyRotation::DOWN) Pitch -= 90.0f;

    // Constrain pitch
    if (Pitch > 89.0f) Pitch = 89.0f;
    if (Pitch < -89.0f) Pitch = -89.0f;

    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    // Calculate new position based on spherical coordinates around the target
    Position.x = Target.x + Radius * cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Position.y = Target.y + Radius * sin(glm::radians(Pitch));
    Position.z = Target.z + Radius * sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));

    // Recalculate camera basis vectors
    Front = glm::normalize(Target - Position);
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up = glm::normalize(glm::cross(Right, Front));
}