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

float stars(vec2 uv, vec2 density, float phase, float threshold) {
    vec2 grid = uv * density;
    vec2 cell = floor(grid);
    float seed = randomCell(cell);
    vec2 position = vec2(randomCell(cell + 4.9), randomCell(cell + 19.1));
    float distanceToStar = length(fract(grid) - (0.2 + position * 0.6));
    float radius = mix(0.064, 0.125, position.y);
    float point = 1.0 - smoothstep(radius * 0.25, radius, distanceToStar);
    float halo = 1.0 - smoothstep(radius, radius * 2.1, distanceToStar);
    float twinkle = 0.43 + 0.57 * (0.5 + 0.5 * sin(phase * (8.0 + floor(seed * 12.0)) + seed * 71.0));
    return (point * 0.86 + halo * 0.14) * twinkle * step(threshold, seed);
}

vec3 sampleArt(vec2 uv) {
    return texture(Sampler0, vec3(clamp(uv, 0.001, 0.999) * 2.0 - 1.0, 1.0)).rgb;
}

void main() {
    vec2 focal = vec2(ProjMat[0][0], ProjMat[1][1]);
    vec2 screen = 0.5 + (viewRay.xy * focal / -viewRay.z) * vec2(0.5, -0.5);
    float ratio = (focal.y / focal.x) / (16.0 / 9.0);
    vec2 cover = vec2(min(1.0, ratio), min(1.0, 1.0 / ratio));
    vec2 uv = 0.5 + (screen - 0.5) * cover + vec2(0.0, 0.078);
    float phase = atan(ModelViewMat[0].z, ModelViewMat[0].x);
    float orbit = phase * 8.0;

    // The near and far sides share an orbit in the projected disk plane.
    vec2 center = vec2(0.50, 0.715);
    vec2 diskStretch = vec2(1.0, 3.75);
    vec2 disk = (uv - center) * diskStretch;
    float radius = length(disk);
    float angle = atan(disk.y, disk.x);
    float rotated = angle - orbit - radius * 1.55;
    vec2 source = center + radius * vec2(cos(rotated), sin(rotated)) / diskStretch;
    vec3 base = sampleArt(uv);
    float luminance = base.r;
    float lit = smoothstep(0.024, 0.24, luminance);
    float band = smoothstep(0.10, 0.27, radius) * (1.0 - smoothstep(0.67, 0.91, radius));
    vec3 orbitTexture = sampleArt(source);
    vec3 color = mix(base, 0.24 * orbitTexture + 0.76 * base, band * lit);
    float filaments = sin(13.0 * (angle - orbit) + radius * 56.0);
    float turbulence = sin(7.0 * angle - 5.0 * orbit - radius * 31.0);
    color *= 1.0 + (0.19 * filaments + 0.10 * turbulence) * band * lit;

    // Light bent above the horizon follows the same rotation.
    vec2 lensed = (uv - center) * vec2(1.0, 0.87);
    float arcRadius = length(lensed);
    float arcAngle = atan(lensed.y, lensed.x);
    float arc = smoothstep(0.11, 0.16, arcRadius) * (1.0 - smoothstep(0.31, 0.41, arcRadius));
    arc *= (1.0 - smoothstep(center.y - 0.025, center.y + 0.015, uv.y)) * lit;
    color *= 1.0 + 0.20 * arc * sin(11.0 * (arcAngle - orbit) + arcRadius * 49.0);

    vec2 shadowPoint = (uv - vec2(0.50, 0.63)) * vec2(1.0, 1.28);
    float sky = smoothstep(0.18, 0.26, length(shadowPoint))
        * (1.0 - smoothstep(0.035, 0.18, luminance));
    float specks = 0.65 * stars(screen, vec2(105.0, 64.0), phase, 0.983)
        + 0.31 * stars(screen + vec2(0.015, 0.033), vec2(181.0, 101.0), phase, 0.975);
    fragColor = vec4(color + vec3(specks * sky), 1.0);
}
