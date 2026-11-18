#version 440

// The Kaleidoscope visual: a Kali-set fractal (p -> |p| / |p|^2 - c) seen
// through a ring of mirrors, in square pixels and the Omarchy theme's
// colours. KaleidoPilot (C++) picks it from the song's file and turns it
// with the music: `fold` is the mirror count (this song's, the last song's,
// how far the crossfade has got) and the spin; `shape` the Kali constant,
// where the bright lines sit and the inner turn; `view` zoom, hue and glow.
// The beat brightens it, the treble lights the lines.
//
// Drawn into a small texture, one fragment per pixel; shaders/pixelgrid.frag
// enlarges it with the gaps between pixels.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float pulse;    // 0..1, the beat
    float level;    // 0..1, loudness
    float high;     // 0..1, treble
    vec2 size;      // the grid, in pixels (one fragment each)
    vec4 fold;      // mirrors, previous mirrors, crossfade 0..1, spin
    vec4 shape;     // c.x, c.y, ring, twist
    vec4 view;      // zoom, hue 0..1, glow, unused
    vec4 c0;        // accent
    vec4 c1;        // cyan
    vec4 c2;        // blue
    vec4 c3;        // magenta
    vec4 c4;        // bright cyan
    vec4 window;    // the window colour
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

vec3 seen(vec2 uv, float mirrors) {
    // Fold the angle into one mirrored wedge.
    float wedge = 6.28318530718 / mirrors;
    float a = mod(atan(uv.y, uv.x) + fold.w, wedge);
    a = min(a, wedge - a);
    vec2 p = vec2(cos(a), sin(a)) * length(uv) * view.x;

    // The Kali set, turned a little each step.
    mat2 turn = mat2(cos(shape.w), -sin(shape.w), sin(shape.w), cos(shape.w));
    float nearest = 1e9, line = 1e9, total = 0.0;
    for (int i = 0; i < 9; ++i) {
        p = abs(p) / max(dot(p, p), 1e-6) - shape.xy;
        p = turn * p;
        float l = length(p);
        nearest = min(nearest, l);
        line = min(line, abs(l - shape.z - 0.5));
        total += l;
    }
    float body = clamp(exp(-nearest * 12.0) * 1.3, 0.0, 1.0);
    float lines = exp(-line * 30.0) * (0.7 + 0.6 * high);
    vec3 base = palette(view.y + total * 0.02 + nearest * 0.5);
    float b = min(1.0, body * 0.85 + lines * 0.55) * view.z * (0.8 + 0.35 * pulse + 0.15 * level);
    vec3 colour = mix(window.rgb, base, clamp(b, 0.0, 1.0));
    return mix(colour, c4.rgb, clamp(lines * body * 0.5, 0.0, 1.0));
}

void main() {
    vec2 id = floor(qt_TexCoord0 * size);
    float side = min(size.x, size.y);
    vec2 uv = (id + 0.5 - size * 0.5) / side;
    float r = length(uv) * 2.0;
    if (r > 1.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 colour = seen(uv, fold.x);
    // A new song's mirrors fade in over the last song's.
    if (fold.z < 1.0)
        colour = mix(seen(uv, fold.y), colour, smoothstep(0.0, 1.0, fold.z));
    colour = min(colour, vec3(0.95));
    float alpha = 1.0 - smoothstep(0.86, 1.0, r);
    fragColor = vec4(colour * alpha, alpha) * qt_Opacity;
}
