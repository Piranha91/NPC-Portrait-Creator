#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aColor;

// NEW: Outputs are now in view space for consistent lighting
out vec3 FragPos_ViewSpace;
out vec3 Normal_ViewSpace;

out vec2 TexCoords;
out vec4 vertexColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // Transform vertex position to view space
    vec4 pos_view = view * model * vec4(aPos, 1.0);
    FragPos_ViewSpace = vec3(pos_view);

    // Transform vertex normal to view space
    Normal_ViewSpace = mat3(transpose(inverse(view * model))) * aNormal;

    // Final clip space position
    gl_Position = projection * pos_view;
    
    // Pass texture coordinates and color through
    TexCoords = aTexCoords;
    vertexColor = aColor;
}