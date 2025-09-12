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
    mat4 skinMatrix = mat4(1.0);
    if(uIsSkinned)
    {
        // Normalize weights to ensure they sum to 1
        float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
        if (totalWeight > 0.0) {
           skinMatrix = (aWeights.x * uBoneMatrices[aBoneIDs.x] +
                         aWeights.y * uBoneMatrices[aBoneIDs.y] +
                         aWeights.z * uBoneMatrices[aBoneIDs.z] +
                         aWeights.w * uBoneMatrices[aBoneIDs.w]) / totalWeight;
        }
    }
    
    mat4 viewModel = view * model * skinMatrix;
    gl_Position = projection * viewModel * vec4(aPos, 1.0);
    FragPos = vec3(viewModel * vec4(aPos, 1.0));

    // Calculate the Normal Matrix for transforming normals to view space
    NormalMatrix = mat3(transpose(inverse(viewModel)));

    // Create the TBN matrix for transforming tangent-space normals to view space
    vec3 T = normalize(NormalMatrix * aTangent);
    vec3 B = normalize(NormalMatrix * aBitangent);
    vec3 N = normalize(NormalMatrix * aNormal);
    TBN = mat3(T, B, N);
    
    TexCoords = aTexCoords;
    vertexColor = aColor;
}

