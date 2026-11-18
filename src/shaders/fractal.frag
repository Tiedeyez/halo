#version 440

// The Fractal visual: a Julia set that never stops changing shape, in square
// pixels, coloured from the Omarchy theme with a pearl's sheen. The formula
// z -> (1 - m) z^2 + m z^3 + c morphs between the square and the cube; c
// and m are flown by FractalPilot (C++), which keeps c on the edge where the
// sets are intricate. The beat flashes the pearl, the treble brightens the
// sheen, loudness sets the glow. Calm or paused, it holds.
//
// Drawn into a small texture, one fragment per pixel of the set;
// shaders/pixelgrid.frag enlarges it with the gaps between pixels.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;     // seconds of music heard
    float pulse;    // 0..1, the beat
    float level;    // 0..1, loudness
    float high;     // 0..1, treble
    vec2 size;      // the grid, in pixels of the set (one fragment each)
    vec4 shape;     // c.x, c.y, morph, unused
    vec4 view;      // zoom (width across the shorter side), angle, unused, unused
    vec4 c0;        // accent
    vec4 c1;        // cyan
    vec4 c2;        // blue
    vec4 c3;        // magenta
    vec4 c4;        // bright cyan
    vec4 c5;        // foreground
    vec4 window;    // the window colour, inside the set
};

vec3 palette(float t) {
    // The theme's accent, cyan, blue and magenta in a loop, eased between.
    t = fract(t) * 4.0;
    int i = int(floor(t));
    float f = smoothstep(0.0, 1.0, fract(t));
    vec3 a = i == 0 ? c0.rgb : i == 1 ? c1.rgb : i == 2 ? c2.rgb : c3.rgb;
    vec3 b = i == 0 ? c1.rgb : i == 1 ? c2.rgb : i == 2 ? c3.rgb : c0.rgb;
    return mix(a, b, f);
}

vec2 mul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }

void main() {
    vec2 id = floor(qt_TexCoord0 * size);
    float side = min(size.x, size.y);
    vec2 uv = (id + 0.5 - size * 0.5) / side;
    // A round window, its edge stepped in whole pixels.
    float r = length(uv) * 2.0;
    if (r > 1.0) {
        fragColor = vec4(0.0);
        return;
    }

    float m = shape.z;
    vec2 c = shape.xy;
    float a = view.y;
    mat2 turn = mat2(cos(a), -sin(a), sin(a), cos(a));
    vec2 z = turn * uv * view.x;
    vec2 dz = vec2(1.0, 0.0);

    // Escape time, with the derivative for the surface normal.
    const int limit = 140;
    int n = 0;
    float m2 = 0.0;
    for (int k = 0; k < limit; ++k) {
        vec2 z2 = mul(z, z);
        vec2 z3 = mul(z2, z);
        // p'(z) = 2(1-m) z + 3m z^2
        dz = mul((2.0 * (1.0 - m)) * z + (3.0 * m) * z2, dz);
        z = (1.0 - m) * z2 + m * z3 + c;
        m2 = dot(z, z);
        if (m2 > 256.0) break;
        n = k + 1;
    }

    vec3 colour;
    float alpha = 1.0;
    if (m2 <= 256.0) {
        // Inside: the window colour, with a breath of the accent on the beat.
        colour = mix(window.rgb, c0.rgb, 0.05 + 0.12 * pulse);
    } else {
        // Smooth escape count for a degree between 2 and 3.
        float degree = 2.0 + m;
        float mu = float(n) + 1.0 - log(log(sqrt(m2)) / log(16.0)) / log(degree);
        float t = mu * 0.045 + time * 0.03;
        vec3 base = palette(t);

        // The pearl: a normal from z / z', lit from the upper left.
        vec2 u = z * vec2(dz.x, -dz.y) / max(dot(dz, dz), 1e-20);
        vec2 normal = normalize(u);
        vec2 light = normalize(vec2(-0.6, 0.8));
        float facing = clamp(dot(normal, light) * 0.5 + 0.5, 0.0, 1.0);
        vec3 film = 0.5 + 0.5 * cos(6.28318 * (facing * 0.7 + t * 0.5 + vec3(0.0, 0.33, 0.67)));
        vec3 pearl = mix(base, base * (0.55 + 0.9 * film), 0.45);
        float sheen = pow(facing, 8.0) * (0.45 + 0.55 * high);
        colour = pearl * (0.6 + 0.5 * facing) + mix(c5.rgb, c4.rgb, 0.5) * sheen * 0.7;

        // Far from the set it fades toward the window, so the edges glow.
        float glow = clamp(mu / 18.0, 0.0, 1.0);
        colour = mix(window.rgb, colour, 0.35 + 0.65 * glow);
    }

    colour *= 0.6 + 0.3 * level + 0.2 * pulse;
    colour = min(colour, vec3(0.95));
    alpha *= 1.0 - smoothstep(0.86, 1.0, r);
    fragColor = vec4(colour * alpha, alpha) * qt_Opacity;
}
