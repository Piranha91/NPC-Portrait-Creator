#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aColor; // FIX: Accept vertex color

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 vertexColor; // FIX: Pass color to fragment shader

uniform mat4 model; // Use this for the object's world transform
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // The final position is a simple, standard calculation.
    // model contains the correct world space for each mesh part.
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // Calculate fragment position in world space for lighting
    FragPos = vec3(model * vec4(aPos, 1.0));

    // For lighting, transform the normal from model space to world space.
    // The mat3() cast removes translation from the matrix.
    Normal = mat3(transpose(inverse(model))) * aNormal;

    TexCoords = aTexCoords;
    vertexColor = aColor; // FIX: Pass vertex color through
}
