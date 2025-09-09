#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

// Data to pass to the fragment shader
out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

// Standard matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// New uniforms for handling model-space normals
uniform bool is_model_space;
uniform mat4 model_transform;

void main()
{
    mat4 final_view = view;
    mat4 final_model = model;

    // If this mesh uses model-space normals, its world transform
    // is combined with the view matrix before projection.
    if (is_model_space) {
        final_view = view * model_transform;
    }

    // Standard calculation for the final vertex position on screen
    gl_Position = projection * final_view * final_model * vec4(aPos, 1.0);

    // Calculate the vertex position in view space (relative to the camera) for lighting
    FragPos = vec3(final_view * final_model * vec4(aPos, 1.0));
    
    // Pass the original, untransformed normal and texture coordinates
    Normal = aNormal;
    TexCoords = aTexCoords;
}