#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aColor;
layout (location = 4) in ivec4 aBoneIDs;
layout (location = 5) in vec4 aWeights;

// Outputs to fragment shader
out vec3 FragPos;
out vec3 Normal; // The original vertex normal, transformed to world space
out vec2 TexCoords;
out vec4 vertexColor;
// Pass the per-vertex normal transformation matrix for model-space normal maps
out mat3 NormalMatrix;

// Uniforms
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Skinning Uniforms
const int MAX_BONES = 80;
uniform bool uIsSkinned;
uniform mat4 uBoneMatrices[MAX_BONES];

void main()
{
    mat4 skinMatrix = mat4(1.0);
    if (uIsSkinned) {
        // Normalize weights to ensure they sum to 1.0
        float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
        vec4 normalizedWeights = (totalWeight > 0.0) ? aWeights / totalWeight : vec4(0.0);

        skinMatrix = normalizedWeights.x * uBoneMatrices[aBoneIDs.x] +
                     normalizedWeights.y * uBoneMatrices[aBoneIDs.y] +
                     normalizedWeights.z * uBoneMatrices[aBoneIDs.z] +
                     normalizedWeights.w * uBoneMatrices[aBoneIDs.w];
    }

    // The final world matrix combines the object's transform and the per-vertex skinning transform
    mat4 finalModelMatrix = model * skinMatrix;

    // Transform position to world space
    vec4 worldPos = finalModelMatrix * vec4(aPos, 1.0);
    FragPos = vec3(worldPos);
    
    // Calculate the normal matrix and pass it to the fragment shader
    // This matrix transforms normals from model space to world space correctly, even with non-uniform scaling.
    NormalMatrix = mat3(transpose(inverse(finalModelMatrix)));
    
    // Transform the base vertex normal to world space and pass it
    Normal = NormalMatrix * aNormal;
    
    // Final clip space position
    gl_Position = projection * view * worldPos;
    
    // Pass texture coordinates and color through
    TexCoords = aTexCoords;
    vertexColor = aColor;
}

