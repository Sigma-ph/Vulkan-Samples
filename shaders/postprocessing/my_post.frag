#version 450

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1) uniform sampler2D DepthTexture;
layout(set = 0, binding = 2) uniform sampler2D ColorTexture;

void main() {
    float depthValue = texture(DepthTexture, TexCoord).r;
    vec4 color = texture(ColorTexture, TexCoord);
    depthValue += 0.4;
    // �������ֵ������Ч
    float fogAmount = smoothstep(0.1, 1.0, depthValue); // ������������ʼ�ͽ������

    // �����ԽС�ĵط���ɫԽ�ӽ��Ұ�ɫ�����Խ��ĵط�Խ�ӽ�ԭʼ��ɫ
    vec3 fogColor = mix(vec3(1.0), color.rgb, fogAmount);

    FragColor = color;
    // FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}