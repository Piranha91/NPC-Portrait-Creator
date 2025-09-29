#include "Camera.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp> // For printing glm::vec3
#include <iostream>

Camera::Camera(glm::vec3 target, float radius, float yaw, float pitch) :
    Target_worldSpace_yUp(target),
    RadiusFromTarget(radius),
    Yaw(yaw),
    Pitch(pitch),
    Front_worldSpace_yUp(glm::vec3(0.0f, 0.0f, -1.0f)),
    WorldUp_yUp(glm::vec3(0.0f, 1.0f, 0.0f)),
    MouseSensitivity(SENSITIVITY),
    PanSensitivity(PAN_SENSITIVITY) {
    // Set a default initial state before any model is loaded
    SetInitialState(glm::vec3(0.0f, 50.0f, 0.0f), DEFAULT_RADIUS, YAW, PITCH);
    // Apply this initial state
    Reset();
}

// This matrix transforms coordinates from the renderer's world space into the camera's
// local view/eye space. It is essential for rendering the scene from the camera's perspective.
//
// Input Space: Renderer's World Space (Y-up)
// Transformation: Applies the inverse of the camera's world position and orientation.
// Output Space: Camera's View Space (Y-up, with the camera at the origin)
glm::mat4 Camera::GetViewMatrix() {
    return glm::lookAt(Position_worldSpace_yUp, Target_worldSpace_yUp, Up_localSpace_yUp);
}

// Saves the calculated "zero position"
void Camera::SetInitialState(glm::vec3 target_worldSpace_yUp, float radiusFromTarget, float yaw, float pitch) {
    m_initialTarget_worldSpace_yUp = target_worldSpace_yUp;
    m_initialRadiusFromTarget = radiusFromTarget;
    m_initialYaw = yaw;
    m_initialPitch = pitch;
}

// Now uses the saved initial state instead of hardcoded values
void Camera::Reset() {
    std::cout << "\n--- Resetting Camera Position ---" << std::endl;
    std::cout << "  [Reset] Target (Y-up): " << glm::to_string(m_initialTarget_worldSpace_yUp) << std::endl;
    std::cout << "  [Reset] Radius:        " << m_initialRadiusFromTarget << std::endl;
    std::cout << "  [Reset] Yaw:           " << m_initialYaw << std::endl;
    std::cout << "  [Reset] Pitch:         " << m_initialPitch << std::endl;
    std::cout << "---------------------------------" << std::endl;

    Target_worldSpace_yUp = m_initialTarget_worldSpace_yUp;
    RadiusFromTarget = m_initialRadiusFromTarget;
    Yaw = m_initialYaw;
    Pitch = m_initialPitch;
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
    std::cout << "Camera Right Vector (Y-up): " << glm::to_string(Right_localSpace_yUp) << std::endl;
    std::cout << "Camera Up Vector (Y-up):    " << glm::to_string(Up_localSpace_yUp) << std::endl;
    std::cout << "-----------------" << std::endl;
    // --- END DEBUGGING ---

    float panSpeed = PanSensitivity * RadiusFromTarget;
    Target_worldSpace_yUp -= Right_localSpace_yUp * (xoffset * panSpeed);
    Target_worldSpace_yUp -= Up_localSpace_yUp * (yoffset * panSpeed);
    updateCameraVectors();
}

void Camera::ProcessMouseScroll(float yoffset) {
    // Adjust radius, scaling zoom speed
    RadiusFromTarget -= yoffset * (RadiusFromTarget * 0.1f);
    if (RadiusFromTarget < 1.0f) RadiusFromTarget = 1.0f; // Prevent zooming too close
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
    // Calculate the new camera Position in world space (Y-up) based on spherical coordinates
    // (Yaw, Pitch, Radius) around the Target point.
    Position_worldSpace_yUp.x = Target_worldSpace_yUp.x + RadiusFromTarget * cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Position_worldSpace_yUp.y = Target_worldSpace_yUp.y + RadiusFromTarget * sin(glm::radians(Pitch));
    Position_worldSpace_yUp.z = Target_worldSpace_yUp.z + RadiusFromTarget * sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));

    // Recalculate camera basis vectors, which also exist in world space (Y-up).
    Front_worldSpace_yUp = glm::normalize(Target_worldSpace_yUp - Position_worldSpace_yUp);
    Right_localSpace_yUp = glm::normalize(glm::cross(Front_worldSpace_yUp, WorldUp_yUp));
    Up_localSpace_yUp = glm::normalize(glm::cross(Right_localSpace_yUp, Front_worldSpace_yUp));
}