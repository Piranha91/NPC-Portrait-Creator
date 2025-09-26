#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 4) in ivec4 aBoneIDs;
layout (location = 5) in vec4 aWeights;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

uniform bool uIsSkinned;
const int MAX_BONES = 80;
uniform mat4 uBoneMatrices[MAX_BONES];

void main()
{
    vec4 vertexPosition = vec4(aPos, 1.0);
    if (uIsSkinned)
    {
        float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
        if (totalWeight > 0.0) {
           mat4 skinMatrix = (aWeights.x * uBoneMatrices[aBoneIDs.x] +
                              aWeights.y * uBoneMatrices[aBoneIDs.y] +
                              aWeights.z * uBoneMatrices[aBoneIDs.z] +
                              aWeights.w * uBoneMatrices[aBoneIDs.w]) / totalWeight;
           vertexPosition = skinMatrix * vertexPosition;
        }
    }
    gl_Position = lightSpaceMatrix * model * vertexPosition;
}