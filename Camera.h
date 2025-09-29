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
    // === Camera Attributes ===
    // Note: All vector attributes are stored in the renderer's World Space, which uses a Y-up axis convention.

    // The camera's position in 3D world space (Y-up).
    glm::vec3 Position_worldSpace_yUp;
    // A normalized vector pointing from the camera's position towards its target (Y-up).
    glm::vec3 Front_worldSpace_yUp;
    // The camera's local 'up' vector, calculated via cross product. Exists in world space coordinates (Y-up).
    glm::vec3 Up_localSpace_yUp;
    // The camera's local 'right' vector, calculated via cross product (Y-up).
    glm::vec3 Right_localSpace_yUp;
    // The constant 'up' direction for the entire world, used for calculations (Y-up).
    glm::vec3 WorldUp_yUp;
    // The point in world space that the camera orbits around and looks at (Y-up).
    glm::vec3 Target_worldSpace_yUp;

    // === Euler Angles ===
    // Yaw is the rotation around the world's Y-axis.
    float Yaw;
    // Pitch is the rotation up or down.
    float Pitch;

    // === Camera options ===
    float MouseSensitivity;
    float PanSensitivity;
    // The distance from the camera's position to its target point (the radius of the orbit).
    float RadiusFromTarget;

    Camera(glm::vec3 target = glm::vec3(0.0f, 50.0f, 0.0f), float radius = DEFAULT_RADIUS, float yaw = YAW, float pitch = PITCH);

    // This matrix transforms coordinates from the renderer's world space into the camera's
    // local view/eye space. It is essential for rendering the scene from the camera's perspective.
    //
    // Input Space: Renderer's World Space (Y-up)
    // Transformation: Applies the inverse of the camera's world position and orientation.
    // Output Space: Camera's View Space (Y-up, with the camera at the origin)
    glm::mat4 GetViewMatrix();

    // Resets the camera's position and orientation to its saved initial state.
    void Reset();
    // Saves a specific camera state (target, distance, orientation) as the new "zero position" for resets.
    void SetInitialState(glm::vec3 target_worldSpace_yUp, float radiusFromTarget, float yaw, float pitch);

    // === Input processing ===
    void ProcessMouseOrbit(float xoffset, float yoffset);
    void ProcessMousePan(float xoffset, float yoffset);
    void ProcessMouseScroll(float yoffset);
    void ProcessKeyRotation(KeyRotation direction);
    // Recalculates the camera's position and basis vectors after a change in orientation or distance.
    void updateCameraVectors();

private:
    // Variables to store the correct "zero position" for the Reset() function.
    // Coordinate Space: Renderer's World Space (Y-up)
    glm::vec3 m_initialTarget_worldSpace_yUp;
    float m_initialRadiusFromTarget;
    float m_initialYaw;
    float m_initialPitch;
};