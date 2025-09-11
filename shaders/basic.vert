#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aColor;
layout (location = 4) in vec4 aTangent;

// NEW: Outputs are now in view space for consistent lighting
out vec3 FragPos;
out vec3 Normal;
out vec3 Tangent_View;        // Pass tangent in view space
out float TangentHandedness;  // Pass handedness separately
out vec2 TexCoords;
out vec4 vertexColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // Transform vertex position to view space
    vec4 pos_view = view * model * vec4(aPos, 1.0);
    FragPos = vec3(pos_view);

    // Transform vertex normal to view space
    mat3 normalMatrix = mat3(transpose(inverse(view * model)));
    Normal = normalMatrix * aNormal;
    
    // Transform tangent to view space and pass handedness
    Tangent_View = normalize(normalMatrix * aTangent.xyz);
    TangentHandedness = aTangent.w;

    // Final clip space position
    gl_Position = projection * pos_view;
    
    // Pass texture coordinates and color through
    TexCoords = aTexCoords;
    vertexColor = aColor;
}