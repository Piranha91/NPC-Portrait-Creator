#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

void main()
{
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(-0.5, -0.8, -0.3));
    float diff = max(dot(normalize(Normal), -lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 1.0, 1.0); // White light
    
    vec3 ambient = 0.2 * vec3(1.0, 1.0, 1.0); // Ambient light
    
    FragColor = vec4(ambient + diffuse, 1.0);
}
