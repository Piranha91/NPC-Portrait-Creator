#version 330 core
out vec4 FragColor;

// Data received from the vertex shader
in vec3 FragPos;
in vec2 TexCoords;
in vec4 vertexColor;
in mat3 TBN;
in mat3 NormalMatrix;

#define MAX_LIGHTS 5

struct Light {
    int type; // 0:disabled, 1:ambient, 2:directional
    vec3 direction; // For directional lights
    vec3 color;
    float intensity;
};

// --- UNIFORMS ---
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal;
uniform sampler2D texture_skin;
uniform sampler2D texture_detail;
uniform sampler2D texture_specular;
uniform sampler2D texture_face_tint;

uniform bool has_normal_map;
uniform bool has_skin_map;
uniform bool has_detail_map;
uniform bool has_specular;
uniform bool has_specular_map;
uniform bool has_face_tint_map;
uniform bool use_alpha_test;
uniform float alpha_threshold;

uniform bool has_tint_color;
uniform vec3 tint_color;

uniform bool is_eye;
uniform float eye_fresnel_strength;
uniform float eye_spec_power;

uniform bool is_model_space;
uniform mat4 view;

uniform vec3 viewPos;

uniform Light lights[MAX_LIGHTS];

void main()
{    
    vec4 baseColor = texture(texture_diffuse1, TexCoords);
    baseColor.rgb *= vertexColor.rgb;
    baseColor.a *= vertexColor.a;

    if (use_alpha_test && baseColor.a < alpha_threshold) {
        discard;
    }

    if (has_tint_color) {
        baseColor.rgb *= tint_color;
    }

    if (has_face_tint_map) {
        vec4 tintSample = texture(texture_face_tint, TexCoords);
        baseColor.rgb = mix(baseColor.rgb, tintSample.rgb, tintSample.a);
    }

    // --- NORMAL CALCULATION ---
    vec3 finalNormal;
    if (has_normal_map) {
        // Sample the normal map. 
        vec3 normal_tangent = texture(texture_normal, TexCoords).rgb;
        
        // For Skyrim's DirectX-style normal maps, the green channel must be inverted for OpenGL.
        normal_tangent.g = 1.0 - normal_tangent.g;

        // Unpack from [0,1] range to [-1,1] range
        normal_tangent = normal_tangent * 2.0 - 1.0;
        
        if (is_model_space) {
             // Model-Space Normal Path (_msn.dds)
            finalNormal = normalize(NormalMatrix * normal_tangent);
        } else {
            // Tangent-Space Normal Path (_n.dds)
            // Use the TBN matrix to transform the normal from tangent space to view space.
            finalNormal = normalize(TBN * normal_tangent);
        }
    } else {
        // If no normal map, just use the vertex normal.
        // It's already been transformed to view space in the vertex shader.
        finalNormal = normalize(TBN[2]);
    }
    
    // --- Dynamic Lighting Calculation (Unchanged) ---
    vec3 finalColor = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (lights[i].type == 0) continue; 

        vec3 lightColor = lights[i].color * lights[i].intensity;

        if (lights[i].type == 1) { // Ambient
            // MODIFICATION: Ambient light should affect the base color, not just be added.
            finalColor += lightColor * baseColor.rgb;
        }
        else if (lights[i].type == 2) { // Directional
            vec3 lightDir_view = normalize(mat3(view) * lights[i].direction);
            float diffuseStrength = max(dot(finalNormal, lightDir_view), 0.0);
            vec3 diffuse = diffuseStrength * lightColor;

            vec3 specular = vec3(0.0);
            if (has_specular) { // Check the flag, not the texture
                float specularStrength = 1.0; // Default strength if no map
                if (has_specular_map) { // Still check for map to get per-texel strength
                    specularStrength = texture(texture_specular, TexCoords).r;
                }
            
                vec3 viewDir = normalize(-FragPos);
                vec3 halfwayDir = normalize(lightDir_view + viewDir);
                float specAmount = pow(max(dot(finalNormal, halfwayDir), 0.0), 32.0);
                specular = specAmount * specularStrength * lightColor;
            }
        finalColor += diffuse * baseColor.rgb + specular;
        }
    }
    
    // Apply subsurface scattering and detail maps after main lighting
    vec3 subsurfaceColor = vec3(0.0);
    if (has_skin_map) {
        subsurfaceColor = texture(texture_skin, TexCoords).r * vec3(1.0, 0.3, 0.2);
        finalColor += subsurfaceColor * baseColor.rgb;
    }
    
    if (has_detail_map) {
        vec3 detailColor = texture(texture_detail, TexCoords).rgb;
        finalColor = mix(finalColor, detailColor, 0.3);
    }

    // --- REMOVED CONFLICTING LINE ---
    // vec3 finalColor = (ambientColor + diffuse + subsurfaceColor) * baseColor.rgb + specular;

    // --- MODIFIED EYE SHADER LOGIC ---
    if (is_eye) {
        vec3 V = normalize(-FragPos);
        float fresnel = pow(1.0 - max(dot(finalNormal, V), 0.0), 5.0);
        
        // Accumulate specular from all directional lights
        vec3 eyeSpecular = vec3(0.0);
        for (int i = 0; i < MAX_LIGHTS; i++) {
            if (lights[i].type == 2) { // If it's a directional light
                vec3 lightColor = lights[i].color * lights[i].intensity;
                vec3 lightDir_view = normalize(mat3(view) * lights[i].direction);
                vec3 halfwayDir = normalize(lightDir_view + V);
                float specAmount = pow(max(dot(finalNormal, halfwayDir), 0.0), eye_spec_power);
                eyeSpecular += specAmount * lightColor;
            }
        }
        finalColor += fresnel * eye_fresnel_strength + eyeSpecular * 0.5;
    }

    FragColor = vec4(finalColor, baseColor.a);
}