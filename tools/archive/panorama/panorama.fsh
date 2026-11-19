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

    // Advect the image locally along the disk. The inner mask keeps the
    // event horizon and its lensed upper rim anchored.
    vec2 disk = (uv - vec2(0.5, 0.73)) * vec2(1.0, 3.0);
    float radius = length(disk);
    float angle = atan(disk.y, disk.x);
    float ring = smoothstep(0.10, 0.22, radius) * (1.0 - smoothstep(0.48, 0.70, radius));
    ring *= smoothstep(0.56, 0.73, uv.y);
    float drift = ring * (0.014 * sin(angle * 4.0 + phase * 12.0 + radius * 19.0)
                        + 0.010 * sin(angle * 7.0 - phase * 20.0 + radius * 31.0));
    vec2 tangent = vec2(-disk.y, disk.x / 3.0);
    vec2 moving = clamp(uv + tangent * drift, 0.001, 0.999);
    vec3 diskColor = texture(Sampler0, vec3(moving.x * 2.0 - 1.0, 1.0 - moving.y * 2.0, 1.0)).rgb;
    float flowLight = 1.0 + ring * 0.17 * sin(angle * 6.0 - phase * 15.0 + radius * 14.0);
    float sky = (1.0 - smoothstep(0.24, 0.54, uv.y)) * (1.0 - smoothstep(0.03, 0.16, diskColor.r));
    float specks = stars(uv + vec2(sin(phase * 2.0), cos(phase * 2.0)) * 0.012, phase) * sky;
    fragColor = vec4(diskColor * flowLight + vec3(specks * 0.52), 1.0);
}
