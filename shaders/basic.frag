#version 330 core
out vec4 FragColor;

// Data received from the vertex shader (now in world space)
in vec3 FragPos;
in vec3 Normal; // Base vertex normal (in world space)
in vec2 TexCoords;
in vec4 vertexColor; 
in mat3 NormalMatrix; // The model-to-world transformation matrix for normals

// --- UNIFORMS ---
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

// Uniforms for eye shading
uniform bool is_eye;
uniform float eye_fresnel_strength;
uniform float eye_spec_power;
uniform bool is_model_space;

// Uniform for camera/view position (in world space)
uniform vec3 viewPos;

// --- LIGHTING (defined in world space) ---
const vec3 lightDir_world = normalize(vec3(0.5, 0.5, 1.0));
const vec3 lightColor = vec3(1.0, 1.0, 1.0);
const vec3 ambientColor = vec3(0.15, 0.15, 0.15);

void main()
{    
    vec4 baseColor = texture(texture_diffuse1, TexCoords);

    // Apply vertex color tint. This is used for effects like hair color gradients.
    baseColor.rgb *= vertexColor.rgb;
    baseColor.a *= vertexColor.a;

    // Alpha Test (for cutout materials like hair and eyelashes)
    if (use_alpha_test && baseColor.a < alpha_threshold) {
        discard;
    }

    // Apply Material Tint Color (e.g., from a BSLightingShaderProperty)
    if (has_tint_color) {
        baseColor.rgb *= tint_color;
    }

    // Apply FaceGen Tint/Makeup. The alpha channel of the tint texture controls the blend.
    if (has_face_tint_map) {
        vec4 tintSample = texture(texture_face_tint, TexCoords);
        baseColor.rgb = mix(baseColor.rgb, tintSample.rgb, tintSample.a);
    }

    // --- NORMAL CALCULATION ---
    // Start with the interpolated world-space vertex normal.
    vec3 finalNormal = normalize(Normal);

    // If a normal map exists, use it to refine the normal.
    if (has_normal_map) {
        // Sample the normal from the texture. The range is [0, 1], so convert to [-1, 1].
        vec3 sampledNormal = texture(texture_normal, TexCoords).rgb * 2.0 - 1.0;

        if (is_model_space) {
            // For a model-space normal map (_msn.dds), the sampled vector is in the model's local space.
            // We transform it to world space using the NormalMatrix calculated in the vertex shader,
            // which correctly includes skinning transformations.
            finalNormal = normalize(NormalMatrix * sampledNormal);
        } else {
            // --- NOTE ON TANGENT-SPACE NORMAL MAPPING ---
            // A full implementation for tangent-space maps (_n.dds) would require a TBN matrix
            // built from vertex normals, tangents, and bitangents. Since tangents are not
            // currently loaded from the NIF, this path is not fully supported and we will
            // fall back to using the base vertex normal. The code is structured to allow
            // for easy addition of TBN logic in the future.
        }
    }
    
    // --- LIGHTING CALCULATIONS (in World Space) ---
    // Diffuse lighting calculation based on the angle between the normal and the light direction.
    float diffuseStrength = max(dot(finalNormal, lightDir_world), 0.0);
    vec3 diffuse = diffuseStrength * lightColor;

    // Specular lighting (Blinn-Phong model) for highlights
    vec3 specular = vec3(0.0);
    if (has_specular_map) {
        float specularStrength = texture(texture_specular, TexCoords).r;
        vec3 viewDir = normalize(viewPos - FragPos);
        vec3 halfwayDir = normalize(lightDir_world + viewDir);
        float specAmount = pow(max(dot(finalNormal, halfwayDir), 0.0), 32.0); // 32.0 is the shininess factor
        specular = specAmount * specularStrength * lightColor;
    }
    
    // Subsurface scattering approximation for skin
    vec3 subsurfaceColor = vec3(0.0);
    if (has_skin_map) {
        // A simple tint based on the skin map's red channel
        subsurfaceColor = texture(texture_skin, TexCoords).r * vec3(1.0, 0.3, 0.2);
    }

    // Detail map, often used for extra texture on landscapes or wood
    if (has_detail_map) {
        vec3 detailColor = texture(texture_detail, TexCoords).rgb;
        baseColor.rgb = mix(baseColor.rgb, detailColor, 0.3); // Blend with base color
    }

    // Final color assembly
    vec3 finalColor = (ambientColor + diffuse + subsurfaceColor) * baseColor.rgb + specular;
    
    // --- EYE SHADING LOGIC ---
    // A separate, additive pass for eye reflections to give them a "wet" look.
    if (is_eye) {
        vec3 viewDir = normalize(viewPos - FragPos);
        // Fresnel term makes edges more reflective as the view angle becomes more oblique.
        float fresnel = pow(1.0 - max(dot(finalNormal, viewDir), 0.0), 5.0);
        // A tighter, stronger specular highlight for the wet look.
        vec3 halfwayDir = normalize(lightDir_world + viewDir);
        float specAmount = pow(max(dot(finalNormal, halfwayDir), 0.0), eye_spec_power);
        vec3 eyeSpecular = specAmount * lightColor;
        // Additively blend the effects onto the final color
        finalColor += fresnel * eye_fresnel_strength + eyeSpecular * 0.5;
    }

    FragColor = vec4(finalColor, baseColor.a);
}

