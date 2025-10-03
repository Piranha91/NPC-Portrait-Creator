#version 330 core
out vec4 FragColor;

// === INPUTS (Varyings from Vertex Shader) ===
// The fragment's position in camera View Space.
in vec3 v_viewSpacePos;
in vec2 TexCoords;
in vec4 vertexColor;
// Transforms a normal from Tangent Space to View Space.
in mat3 v_tangentToViewMatrix;
// Transforms a normal from Model Space to View Space.
in mat3 v_modelToViewNormalMatrix;
// The fragment's position in the light's Clip Space.
in vec4 v_lightClipSpacePos;

#define MAX_LIGHTS 5

struct Light {
    int type; // 0:disabled, 1:ambient, 2:directional
    // Light direction is pre-transformed into View Space on the CPU.
    vec3 direction;
    vec3 color;
    float intensity; };

// --- TEXTURE SAMPLERS ---
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal;
uniform sampler2D texture_skin;
uniform sampler2D texture_detail;
uniform sampler2D texture_specular;
uniform sampler2D texture_face_tint;
uniform sampler2D texture_envmap_2d; // For 2D spherical maps
uniform samplerCube texture_envmap_cube; // For cubemaps
uniform sampler2D texture_envmask;
uniform sampler2D shadowMap;

// --- MATERIAL FLAGS (from NIF file) ---
uniform bool has_normal_map;
uniform bool has_skin_map;
uniform bool has_detail_map;
uniform bool has_specular;
uniform bool has_specular_map;
uniform bool has_face_tint_map;
uniform bool has_greyscale_to_palette;
uniform bool has_environment_map;
uniform bool has_eye_environment_map;
uniform bool is_envmap_cube;
uniform bool has_env_mask; // Environment map mask
uniform bool has_tint_color;
uniform bool has_emissive;
uniform bool is_model_space; // For _msn.dds normal maps

// --- RENDERER TOGGLES (from UI) ---
uniform bool use_alpha_test;
uniform bool u_useDiffuseMap;
uniform bool u_useNormalMap;
uniform bool u_useSkinMap;
uniform bool u_useDetailMap;
uniform bool u_useSpecularMap;
uniform bool u_useFaceTintMap;
uniform bool u_useEnvironmentMap;
uniform bool u_useEmissive;

// --- MATERIAL PROPERTIES ---
uniform float alpha_threshold;
uniform float envMapScale;
uniform float eyeCubemapScale; // NEW: Separate scale for eyes
uniform float greyscaleToPaletteScale;
uniform vec3 tint_color;
uniform vec3 emissiveColor;
uniform float emissiveMultiple;
uniform float materialGlossiness;
uniform float materialSpecularStrength;

// --- EYE SHADER PROPERTIES ---
uniform bool is_eye;
uniform float eye_fresnel_strength;
uniform float eye_spec_power;

// --- GENERAL UNIFORMS ---
// Transforms from World Space to View Space. Used to get reflection vectors back into world space for cubemapping.
uniform mat4 u_view_worldToView;
// The camera's position in World Space. NOTE: Not used in current view-space lighting model.
uniform vec3 u_cameraPos_worldSpace_yUp;
uniform Light lights[MAX_LIGHTS];

// Calculates the shadow contribution (1.0 = lit, 0.0 = in shadow).
float calculateShadow(vec4 pos_lightClipSpace)
{
    // Perspective divide to get Normalized Device Coordinates (NDC) from the light's perspective.
    vec3 pos_lightNDC = pos_lightClipSpace.xyz / pos_lightClipSpace.w;
    // Transform from [-1,1] range to [0,1] range to use as texture coordinates.
pos_lightNDC = pos_lightNDC * 0.5 + 0.5;
    // If the fragment is outside the shadow map's frustum, it's considered lit.
if (pos_lightNDC.z > 1.0) {
        return 1.0; }

    // Get the closest depth stored in the shadow map at this position.
    float closestDepth = texture(shadowMap, pos_lightNDC.xy).r;
    // Get the current fragment's depth from the light's perspective.
float currentDepth = pos_lightNDC.z;
    // Add a small bias to prevent "shadow acne" self-shadowing artifacts.
float bias = 0.005;
    float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;

    return shadow; }


void main()
{
    // --- 1. BASE COLOR & ALPHA TEST ---
    vec4 baseColor = vec4(1.0);
if (u_useDiffuseMap) {
        baseColor = texture(texture_diffuse1, TexCoords); }
    baseColor.rgb *= vertexColor.rgb;
    baseColor.a *= vertexColor.a;
if (use_alpha_test && baseColor.a < alpha_threshold) {
        discard; }

    // Implement Greyscale-to-Palette logic
    if (has_greyscale_to_palette) {
        // This path is for assets like hair that use a greyscale texture for masking a tint color.
        // We use the red channel of the texture as an intensity mask, multiply it by the tint color,
        // and then scale it by the material's 'greyscaleToPaletteScale' property.
        baseColor.rgb = baseColor.rrr * tint_color * greyscaleToPaletteScale;
    } else if (has_tint_color) {
        // This is the fallback for simpler tinting (like for skin), which just multiplies the existing color.
        baseColor.rgb *= tint_color; 
    }

    if (has_face_tint_map && u_useFaceTintMap) {
        vec4 tintSample = texture(texture_face_tint, TexCoords);
        // Mix the base color with the tint color based on the tint mask's alpha channel.
baseColor.rgb = mix(baseColor.rgb, tintSample.rgb, tintSample.a);
    }

    // --- 2. NORMAL CALCULATION ---
    // This will be the final normal vector in View Space, used for all lighting.
    vec3 normal_viewSpace;
if (has_normal_map && u_useNormalMap) {
        // Sample the normal from the texture. It's stored in Tangent Space.
vec3 normal_tangentSpace = texture(texture_normal, TexCoords).rgb;
        
        // For Skyrim's DirectX-style normal maps, the green channel must be inverted for OpenGL.
normal_tangentSpace.g = 1.0 - normal_tangentSpace.g;

        // Unpack from [0,1] color range to [-1,1] vector range.
        normal_tangentSpace = normal_tangentSpace * 2.0 - 1.0;
if (is_model_space) {
             // Model-Space Normal Path (_msn.dds): Transform from Model Space to View Space.
            normal_viewSpace = normalize(v_modelToViewNormalMatrix * normal_tangentSpace);
} else {
            // Tangent-Space Normal Path (_n.dds): Transform from Tangent Space to View Space.
normal_viewSpace = normalize(v_tangentToViewMatrix * normal_tangentSpace);
        }
    } else {
        // If no normal map, just use the vertex normal, which is the 3rd column (index 2) of the TBN matrix.
normal_viewSpace = normalize(v_tangentToViewMatrix[2]); }

    // For eye meshes, normals are often inverted in the NIF to make environment maps work.
// We flip them back for correct lighting.
if (is_eye) {
        //normal_viewSpace = -normal_viewSpace;
}
    
    // --- 3. DYNAMIC LIGHTING ---
    vec3 finalColor = vec3(0.0);
    // Calculate shadow factor once for all lights.
    float shadow = calculateShadow(v_lightClipSpacePos);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (lights[i].type == 0) continue; // Skip disabled lights
        vec3 lightColor = lights[i].color * lights[i].intensity;

        if (lights[i].type == 1) { // Ambient Light
            finalColor += lightColor * baseColor.rgb;
        }
        else if (lights[i].type == 2) { // Directional Light
            // The direction uniform is pre-transformed to View Space on the CPU.
            vec3 lightDir_viewSpace = normalize(lights[i].direction);
            float diffuseStrength = max(dot(normal_viewSpace, lightDir_viewSpace), 0.0);
            vec3 diffuse = diffuseStrength * lightColor;

            vec3 specular = vec3(0.0);
            if (has_specular && u_useSpecularMap) {
                float specularStrength = 1.0;
                if (has_specular_map) {
                    // The specular map's red channel acts as a per-pixel mask or intensity scalar.
                    specularStrength = texture(texture_specular, TexCoords).r;
                }
            
                // In View Space, the view direction is simply the vector from the fragment position to the origin.
                vec3 viewDir_viewSpace = normalize(-v_viewSpacePos);
                vec3 halfwayDir_viewSpace = normalize(lightDir_viewSpace + viewDir_viewSpace);

                // MODIFIED: Use the 'materialGlossiness' uniform from the NIF instead of a hard-coded exponent.
                // This value controls the shininess or tightness of the highlight.
                float specAmount = pow(max(dot(normal_viewSpace, halfwayDir_viewSpace), 0.0), materialGlossiness);
                
                // MODIFIED: Modulate the final specular color by the material's overall specular strength.
                specular = specAmount * specularStrength * lightColor * materialSpecularStrength;
            }
            
            // Apply diffuse and specular, both modulated by the shadow factor and base color.
            finalColor += (diffuse * shadow + specular * shadow) * baseColor.rgb;
        }
    }
    
    // --- 4. POST-LIGHTING EFFECTS ---
    vec3 subsurfaceColor = vec3(0.0);
if (has_skin_map && u_useSkinMap) {
        subsurfaceColor = texture(texture_skin, TexCoords).r * vec3(1.0, 0.3, 0.2); // Reddish tint
finalColor += subsurfaceColor * baseColor.rgb;
    }
    
    if (has_detail_map && u_useDetailMap) {
        vec3 detailColor = texture(texture_detail, TexCoords).rgb;
        // Mix the detail map over the existing color.
finalColor = mix(finalColor, detailColor, 0.3);
    }

     // --- 5. ENVIRONMENT MAPPING / REFLECTIONS (OVERHAULED) ---
    if (u_useEnvironmentMap) {
        if (has_eye_environment_map && is_envmap_cube) {
            // --- Eye-Specific Cubemap Reflection ---
            // This path uses a simpler, additive reflection model with its own unique scale factor.
            vec3 viewDir_viewSpace = normalize(-v_viewSpacePos);
            vec3 reflectDir_viewSpace = reflect(-viewDir_viewSpace, normal_viewSpace);
            
            // Transform reflection vector from View Space back to World Space for cubemap sampling.
            vec3 reflectDir_worldSpace = inverse(mat3(u_view_worldToView)) * reflectDir_viewSpace;
            vec3 envColor = texture(texture_envmap_cube, reflectDir_worldSpace).rgb;
            
            // Add the reflection using the dedicated eye cubemap scale.
            finalColor += envColor * eyeCubemapScale;

        } else if (has_environment_map) {
            // --- Regular Environment Mapping (2D or Cube) ---
            vec3 viewDir_viewSpace = normalize(-v_viewSpacePos);
            vec3 reflectDir_viewSpace = reflect(-viewDir_viewSpace, normal_viewSpace);
            vec3 envColor;

            if (is_envmap_cube) {
                // For cubemaps, sample using the 3D reflection vector in world space.
                vec3 reflectDir_worldSpace = inverse(mat3(u_view_worldToView)) * reflectDir_viewSpace;
                envColor = texture(texture_envmap_cube, reflectDir_worldSpace).rgb;
            } else { 
                // For 2D spherical maps, calculate 2D texture coordinates from the reflection vector.
                vec2 envCoords = normalize(reflectDir_viewSpace.xy) * 0.5 + 0.5;
                envColor = texture(texture_envmap_2d, envCoords).rgb;
            }

            // The reflection's strength is controlled ONLY by the environment mask texture from slot 5.
            // There is no longer a fallback to the specular map.
            float reflectionStrength = 1.0; // Default to full strength if no mask.
            if (has_env_mask) {
                // If a mask texture is present, its red channel determines the reflection intensity.
                reflectionStrength = texture(texture_envmask, TexCoords).r;
            }
            
            // Add the reflection using the general environment map scale.
            finalColor += envColor * reflectionStrength * envMapScale;
        }
    }

    // --- 6. EMISSIVE COLOR ---
    // This is added last as it's independent of lighting and shadows.
if (has_emissive && u_useEmissive) {
        finalColor += emissiveColor * emissiveMultiple; }

    // --- 7. FINAL OUTPUT ---
    FragColor = vec4(finalColor, baseColor.a);
}