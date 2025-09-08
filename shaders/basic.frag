#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

// Samplers for all our textures
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal;
uniform sampler2D texture_skin;
uniform sampler2D texture_detail;
uniform sampler2D texture_specular;

// Flags to tell the shader which maps are available for the current mesh
uniform bool has_normal_map;
uniform bool has_skin_map;
uniform bool has_detail_map;
uniform bool has_specular_map;

// A simple directional light for basic shading
const vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
const vec3 lightColor = vec3(1.0, 1.0, 1.0);
const vec3 ambientColor = vec3(0.4, 0.4, 0.4);

void main()
{    
    // 1. Get base diffuse color
    vec4 baseColor = texture(texture_diffuse1, TexCoords);

    // 2. Get normal from normal map
    vec3 normal = normalize(Normal);
    if (has_normal_map) {
        // Unpack normal from texture (range [0,1] to [-1,1])
        normal = normalize(texture(texture_normal, TexCoords).xyz * 2.0 - 1.0);
    }

    // 3. Apply detail map
    if (has_detail_map) {
        // Detail maps are often grayscale and overlayed
        vec3 detailColor = texture(texture_detail, TexCoords).rgb;
        baseColor.rgb = mix(baseColor.rgb, detailColor, 0.3); // Mix 30% of detail
    }

    // 4. Basic lighting calculation
    float diffuseStrength = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffuseStrength * lightColor;

    // 5. Get specular highlights
    float specularStrength = 0.0;
    if (has_specular_map) {
        // The specular map often stores glossiness in the alpha channel
        specularStrength = texture(texture_specular, TexCoords).r;
    }
    
    // 6. Subsurface scattering (simplified for skin)
    vec3 subsurfaceColor = vec3(0.0);
    if (has_skin_map) {
        // Red channel of _sk map is subsurface color, Green is tint mask
        subsurfaceColor = texture(texture_skin, TexCoords).r * vec3(1.0, 0.3, 0.2);
    }

    // Combine all components
    vec3 finalColor = (ambientColor + diffuse + subsurfaceColor) * baseColor.rgb + (specularStrength * lightColor);

    FragColor = vec4(finalColor, baseColor.a);
}