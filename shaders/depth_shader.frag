#version 330 core

void main()
{
    // This fragment shader is intentionally empty.
    // For depth-only passes (like shadow map generation), we only care about the
    // depth value of each fragment, which is automatically handled and written
    // to the depth buffer after the vertex shader stage. No color output is needed.
}