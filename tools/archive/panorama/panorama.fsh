#version 330
#extension GL_ARB_separate_shader_objects : require

#include <minecraft:dynamictransforms.glsl>
#include <minecraft:projection.glsl>

uniform samplerCube Sampler0;
layout(location = 0) in vec3 viewRay;
layout(location = 0) out vec4 fragColor;

float randomCell(vec2 cell) {
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

float stars(vec2 uv, float phase) {
    vec2 grid = uv * vec2(176.0, 99.0);
    vec2 cell = floor(grid);
    float seed = randomCell(cell);
    vec2 position = vec2(randomCell(cell + 4.9), randomCell(cell + 19.1));
    float distanceToStar = length(fract(grid) - (0.2 + position * 0.6));
    float radius = mix(0.055, 0.12, position.y);
    float point = 1.0 - smoothstep(0.0, radius, distanceToStar);
    float twinkle = 0.55 + 0.45 * sin(phase * (7.0 + floor(seed * 8.0)) + seed * 71.0);
    return point * twinkle * smoothstep(0.975, 1.0, seed);
}

void main() {
    // Reproject in view space so the vanilla panorama yaw supplies time
    // without moving the event horizon across the screen.
    vec2 focal = vec2(ProjMat[0][0], ProjMat[1][1]);
    vec2 screen = 0.5 + (viewRay.xy * focal / -viewRay.z) * vec2(0.5, -0.5);
    float ratio = (focal.y / focal.x) / (16.0 / 9.0);
    vec2 cover = vec2(min(1.0, ratio), min(1.0, 1.0 / ratio));
    vec2 uv = 0.5 + (screen - 0.5) * cover;
    float phase = atan(ModelViewMat[0].z, ModelViewMat[0].x);

    // Rotate the disk texture in its projected orbital plane.
    // The background and the dark horizon stay in view space.
    vec2 disk = (uv - vec2(0.5, 0.73)) * vec2(1.0, 4.2);
    float radius = length(disk);
    float angle = atan(disk.y, disk.x);
    float orbit = phase * 8.0;
    float rotated = angle - orbit - radius * 1.7;
    vec2 source = vec2(0.5, 0.73) + radius * vec2(cos(rotated), sin(rotated) / 4.2);
    vec2 textureUv = uv;
    float mask = smoothstep(0.12, 0.29, radius) * (1.0 - smoothstep(0.58, 0.72, radius));
    mask *= smoothstep(0.60, 0.73, uv.y);
    vec3 base = texture(Sampler0, vec3(textureUv.x * 2.0 - 1.0, textureUv.y * 2.0 - 1.0, 1.0)).rgb;
    vec3 orbitTexture = texture(Sampler0, vec3(source.x * 2.0 - 1.0, source.y * 2.0 - 1.0, 1.0)).rgb;
    vec3 color = mix(base, orbitTexture, 0.46 * mask * smoothstep(0.02, 0.23, base.r));
    float filaments = sin(11.0 * (angle - orbit) + radius * 58.0) * 0.15;
    color *= 1.0 + filaments * mask;
    float sky = (1.0 - smoothstep(0.20, 0.54, uv.y)) * (1.0 - smoothstep(0.03, 0.16, base.r));
    float specks = stars(uv, phase) * sky;
    fragColor = vec4(color + vec3(specks * 0.52), 1.0);
}
