#version 440

// The pixel glitch Halo uses for changes: a song, a view, the wordmark.
// At amount 0 the source passes through untouched; towards 1 it breaks into
// big pixels, slides horizontal slices sideways, splits red from blue and
// flashes a few slices in the theme's accent, like Omarchy's screensaver
// effects tearing the logo apart.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float amount;
    float seed;
    vec2 size;
    vec4 tint;
};

layout(binding = 1) uniform sampler2D source;

float hash(float n) {
    return fract(sin(n * 12.9898 + seed * 78.233) * 43758.5453);
}

void main() {
    vec2 uv = qt_TexCoord0;
    float a = clamp(amount, 0.0, 1.0);

    // Slices of 6 to 28 px; the share that moves grows with the amount.
    float sliceHeight = mix(28.0, 6.0, hash(floor(seed * 7.0) + 3.0));
    float row = floor(uv.y * size.y / sliceHeight);
    float r = hash(row);
    float shift = r > 1.0 - a * 0.55 ? (hash(row + 17.0) - 0.5) * 0.22 * a : 0.0;
    uv.x += shift;

    // Big square pixels, snapped to whole screen pixels so edges stay crisp.
    float block = floor(a * a * 26.0);
    if (block >= 2.0) {
        vec2 cell = vec2(block) / size;
        uv = (floor(uv / cell) + 0.5) * cell;
    }

    float split = a * 0.014 + abs(shift) * 0.08;
    vec4 c = texture(source, uv);
    c.r = texture(source, uv + vec2(split, 0.0)).r;
    c.b = texture(source, uv - vec2(split, 0.0)).b;

    // A few slices light up in the accent (colours are premultiplied).
    if (r > 1.0 - a * 0.14)
        c.rgb = mix(c.rgb, tint.rgb * c.a, 0.65);

    if (uv.x < 0.0 || uv.x > 1.0)
        c = vec4(0.0);
    fragColor = c * qt_Opacity;
}
