#version 330 core
out vec4 FragColor;

// Data received from the vertex shader
in vec3 FragPos;
in vec3 Normal;
in vec4 Tangent;
in vec2 TexCoords;
in vec4 vertexColor; // FIX: Receive vertex color

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
uniform mat4 model; // The model matrix is now needed here
uniform mat4 view; // The view matrix is now needed here

// --- Add a new uniform for the camera/view position ---
uniform vec3 viewPos;

// --- LIGHTING ---
// Light is defined in world space
const vec3 lightDir_world = normalize(vec3(0.5, 0.5, 1.0));
const vec3 lightColor = vec3(1.0, 1.0, 1.0);
const vec3 ambientColor = vec3(0.15, 0.15, 0.15);

void main()
{    
    vec4 baseColor = texture(texture_diffuse1, TexCoords);

    // --- FIX: APPLY VERTEX COLOR TINT ---
    // Multiply the texture color by the vertex's RGB color.
    baseColor.rgb *= vertexColor.rgb;

    // This allows for soft, faded edges on hair and scalps.
    baseColor.a *= vertexColor.a;

    // Alpha Test (for cutout materials like hair and eyelashes)
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
        vec3 modelSpaceNormal = texture(texture_normal, TexCoords).rgb * 2.0 - 1.0;
        
        // 3. Transform the normal from model space to view space using the full normal matrix.
        mat3 normalMatrix = mat3(transpose(inverse(view * model)));
        finalNormal = normalize(normalMatrix * modelSpaceNormal);
    } else {
        // --- STANDARD TANGENT-SPACE PATH ---
        vec3 N = normalize(Normal);
        if (has_normal_map) {
            // Sample normal from map (in tangent space)
            vec3 n_ts = texture(texture_normal, TexCoords).xyz * 2.0 - 1.0;

            // Create TBN matrix to transform from tangent to view space
            vec3 T = normalize(Tangent.xyz);
            vec3 B = normalize(cross(N, T)) * Tangent.w; // Use handedness
            mat3 TBN = mat3(T, B, N);

            // Transform normal and re-normalize
            finalNormal = normalize(TBN * n_ts);
        } else {
            // No normal map, just use vertex normal
            finalNormal = N;
        }
    }
    
    // --- LIGHTING CALCULATIONS ---
    // Transform the world-space light direction into view space to match our normals
    vec3 lightDir_view = normalize(mat3(view) * lightDir_world);
    float diffuseStrength = max(dot(finalNormal, lightDir_view), 0.0);
    vec3 diffuse = diffuseStrength * lightColor;

    // --- FIX: REWORK SPECULAR CALCULATION ---
    vec3 specular = vec3(0.0);
    if (has_specular_map) {
        float specularStrength = texture(texture_specular, TexCoords).r;

        // We need the view direction in the same space as the normal and light (view space)
        // Since FragPos is in world space, transform it to view space first.
        vec3 fragPos_view = vec3(view * vec4(FragPos, 1.0));
        vec3 viewDir = normalize(-fragPos_view); // The view vector is from the fragment to the camera (which is at 0,0,0 in view space)

        // Blinn-Phong calculation
        vec3 halfwayDir = normalize(lightDir_view + viewDir);
        float specAmount = pow(max(dot(finalNormal, halfwayDir), 0.0), 32.0); // 32.0 is the "shininess" factor. Higher is smaller/sharper.
        specular = specAmount * specularStrength * lightColor;
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
    vec3 finalColor = (ambientColor + diffuse + subsurfaceColor) * baseColor.rgb + specular; // Add the new specular component
    FragColor = vec4(finalColor, baseColor.a);
}

