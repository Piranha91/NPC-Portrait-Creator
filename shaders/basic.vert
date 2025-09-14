#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aColor;
layout (location = 4) in ivec4 aBoneIDs;
layout (location = 5) in vec4 aWeights;
layout (location = 6) in vec3 aTangent;
layout (location = 7) in vec3 aBitangent;

out vec3 FragPos;
out vec2 TexCoords;
out vec4 vertexColor;
out mat3 TBN;           // For tangent-space normal mapping
out mat3 NormalMatrix;  // For model-space normal mapping

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform bool uIsSkinned;
const int MAX_BONES = 80;
uniform mat4 uBoneMatrices[MAX_BONES];

void main()
{
    // 1. Start with the original vertex position.
    vec4 vertexPosition = vec4(aPos, 1.0);

    // 2. If the mesh is skinned, calculate the skinned position IN MODEL SPACE.
    if (uIsSkinned)
    {
        // Normalize weights to ensure they sum to 1
        float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
        if (totalWeight > 0.0) {
           mat4 skinMatrix = (aWeights.x * uBoneMatrices[aBoneIDs.x] +
                              aWeights.y * uBoneMatrices[aBoneIDs.y] +
                              aWeights.z * uBoneMatrices[aBoneIDs.z] +
                              aWeights.w * uBoneMatrices[aBoneIDs.w]) / totalWeight;
            
           vertexPosition = skinMatrix * vertexPosition;
        }
    }
    
    // 3. Apply the standard Model-View-Projection transformation.
    gl_Position = projection * view * model * vertexPosition;

    // 4. Calculate values needed for fragment shader lighting.
    FragPos = vec3(view * model * vertexPosition);
    NormalMatrix = mat3(transpose(inverse(view * model)));

    // Create the TBN matrix for transforming tangent-space normals to view space
    vec3 T = normalize(NormalMatrix * aTangent);
    vec3 B = normalize(NormalMatrix * aBitangent);
    vec3 N = normalize(NormalMatrix * aNormal);
    TBN = mat3(T, B, N);
    
    TexCoords = aTexCoords;
    vertexColor = aColor;
}

