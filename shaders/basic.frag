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
uniform sampler2D texture_face_tint; // Add this line

// Flags to tell the shader which maps are available for the current mesh
uniform bool has_normal_map;
uniform bool has_skin_map;
uniform bool has_detail_map;
uniform bool has_specular_map;
uniform bool has_face_tint_map; // Add this line

// Uniforms for alpha testing
uniform bool use_alpha_test;
uniform float alpha_threshold;

// Uniforms for Tinting
uniform bool has_tint_color;
uniform vec3 tint_color;

// A simple directional light for basic shading
const vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
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
        // Sample the tint color (makeup, dirt, etc.)
        vec4 tintSample = texture(texture_face_tint, TexCoords);
        // Use the tint's alpha channel to blend it over the base skin color
        baseColor.rgb = mix(baseColor.rgb, tintSample.rgb, tintSample.a);
    }

    vec3 normal = normalize(Normal);
    if (has_normal_map) {
        normal = normalize(texture(texture_normal, TexCoords).xyz * 2.0 - 1.0);
    }

    if (has_detail_map) {
        vec3 detailColor = texture(texture_detail, TexCoords).rgb;
        baseColor.rgb = mix(baseColor.rgb, detailColor, 0.3);
    }

    float diffuseStrength = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffuseStrength * lightColor;

    float specularStrength = 0.0;
    if (has_specular_map) {
        specularStrength = texture(texture_specular, TexCoords).r;
    }
    
    vec3 subsurfaceColor = vec3(0.0);
    if (has_skin_map) {
        subsurfaceColor = texture(texture_skin, TexCoords).r * vec3(1.0, 0.3, 0.2);
    }

    vec3 finalColor = (ambientColor + diffuse + subsurfaceColor) * baseColor.rgb + (specularStrength * lightColor);

    FragColor = vec4(finalColor, baseColor.a);
}