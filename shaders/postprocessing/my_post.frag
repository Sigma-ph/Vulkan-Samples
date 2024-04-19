#version 450

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1) uniform sampler2D DepthTexture;
layout(set = 0, binding = 2) uniform sampler2D ColorTexture;

void main() {
    float depthValue = texture(DepthTexture, TexCoord).r;
    vec4 color = texture(ColorTexture, TexCoord);
    depthValue += 0.4;
    // 根据深度值计算雾效
    float fogAmount = smoothstep(0.1, 1.0, depthValue); // 调整深度雾的起始和结束深度

    // 将深度越小的地方颜色越接近灰白色，深度越大的地方越接近原始颜色
    vec3 fogColor = mix(vec3(1.0), color.rgb, fogAmount);

    FragColor = color;
    // FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}