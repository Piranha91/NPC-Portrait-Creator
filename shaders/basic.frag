#version 330 core
out vec4 FragColor;

// Data received from the vertex shader
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

// --- UNIFORMS ---
// Samplers for all textures
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal;
uniform sampler2D texture_skin;
uniform sampler2D texture_detail;
uniform sampler2D texture_specular;
uniform sampler2D texture_face_tint;

// Flags for available maps
uniform bool has_normal_map;
uniform bool has_skin_map;
uniform bool has_detail_map;
uniform bool has_specular_map;
uniform bool has_face_tint_map;

// Uniforms for alpha testing
uniform bool use_alpha_test;
uniform float alpha_threshold;

// Uniforms for tinting
uniform bool has_tint_color;
uniform vec3 tint_color;

// New uniforms for model-space normal (MSN) logic
uniform bool is_model_space;
uniform mat4 view; // The view matrix is now needed here

// --- LIGHTING ---
// Light is defined in world space
const vec3 lightDir_world = normalize(vec3(0.5, 0.5, 1.0));
const vec3 lightColor = vec3(1.0, 1.0, 1.0);
const vec3 ambientColor = vec3(0.4, 0.4, 0.4);

void main()
{    
    vec4 baseColor = texture(texture_diffuse1, TexCoords);

    // Alpha Test
    if (use_alpha_test && baseColor.a < alpha_threshold) {
        discard;
    }

    // Apply Tint Color
    if (has_tint_color) {
        baseColor.rgb *= tint_color;
    }

    // Apply FaceGen Tint/Makeup
    if (has_face_tint_map) {
        vec4 tintSample = texture(texture_face_tint, TexCoords);
        baseColor.rgb = mix(baseColor.rgb, tintSample.rgb, tintSample.a);
    }

    // --- NORMAL CALCULATION ---
    vec3 finalNormal;
    if (is_model_space) {
        // --- MODEL-SPACE NORMAL PATH ---
        // 1. Sample the normal map, which contains normals in the model's local space.
        // 2. The .rbg swizzle is required to match Skyrim's format.
        vec3 modelSpaceNormal = texture(texture_normal, TexCoords).rbg * 2.0 - 1.0;
        // 3. Transform the normal directly from model space to view space.
        finalNormal = normalize(mat3(view) * modelSpaceNormal);
    } else {
        // --- STANDARD TANGENT-SPACE PATH ---
        // (This is a simplified implementation for standard objects)
        finalNormal = normalize(Normal);
        if (has_normal_map) {
            // For a full implementation, this would require a TBN matrix.
            // For now, we assume the normal map directly perturbs the vertex normal.
            finalNormal = normalize(texture(texture_normal, TexCoords).xyz * 2.0 - 1.0);
        }
    }
    
    // --- LIGHTING CALCULATIONS ---
    // Transform the world-space light direction into view space to match our normals
    vec3 lightDir_view = normalize(mat3(view) * lightDir_world);
    float diffuseStrength = max(dot(finalNormal, lightDir_view), 0.0);
    vec3 diffuse = diffuseStrength * lightColor;

    float specularStrength = 0.0;
    if (has_specular_map) {
        specularStrength = texture(texture_specular, TexCoords).r;
    }
    
    vec3 subsurfaceColor = vec3(0.0);
    if (has_skin_map) {
        subsurfaceColor = texture(texture_skin, TexCoords).r * vec3(1.0, 0.3, 0.2);
    }

    if (has_detail_map) {
        vec3 detailColor = texture(texture_detail, TexCoords).rgb;
        baseColor.rgb = mix(baseColor.rgb, detailColor, 0.3);
    }

    // Final color assembly
    vec3 finalColor = (ambientColor + diffuse + subsurfaceColor) * baseColor.rgb + (specularStrength * lightColor);
    FragColor = vec4(finalColor, baseColor.a);
}