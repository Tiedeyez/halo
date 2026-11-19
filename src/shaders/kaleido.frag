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
    vec4 view;      // zoom, hue 0..1, glow, kick (1 on a beat, falling fast)
    vec4 c0;        // accent
    vec4 c1;        // cyan
    vec4 c2;        // blue
    vec4 c3;        // magenta
    vec4 c4;        // bright cyan
    vec4 c5;        // the theme's foreground, for glints
    vec4 window;    // the window colour
};

// Mixing in a square-root space keeps blends between two theme colours
// bright instead of muddy (close to mixing light, not paint).
vec3 blend(vec3 a, vec3 b, float t) {
    return pow(mix(sqrt(a), sqrt(b), t), vec3(2.0));
}

vec3 palette(float t) {
    // The accent leads, then the theme's magenta, its bright cyan and
    // magenta again, so neighbouring layers always differ in hue; blue is
    // kept for the shadows.
    t = fract(t) * 4.0;
    int i = int(floor(t));
    float f = smoothstep(0.0, 1.0, fract(t));
    vec3 a = i == 0 ? c0.rgb : i == 1 ? c3.rgb : i == 2 ? c4.rgb : c3.rgb;
    vec3 b = i == 0 ? c3.rgb : i == 1 ? c4.rgb : i == 2 ? c3.rgb : c0.rgb;
    return blend(a, b, f);
}

vec3 seen(vec2 uv, float mirrors) {
    // Fold the angle into one mirrored wedge.
    float wedge = 6.28318530718 / mirrors;
    float a = mod(atan(uv.y, uv.x) + fold.w, wedge);
    a = min(a, wedge - a);
    vec2 p = vec2(cos(a), sin(a)) * length(uv) * view.x;

    // The Kali set, turned a little each step.
    mat2 turn = mat2(cos(shape.w), -sin(shape.w), sin(shape.w), cos(shape.w));
    float nearest = 1e9, line = 1e9, total = 0.0, step = 0.0;
    for (int i = 0; i < 9; ++i) {
        p = abs(p) / max(dot(p, p), 1e-6) - shape.xy;
        p = turn * p;
        float l = length(p);
        // Which step came nearest picks the colour band, so each layer of
        // the fractal reads as its own theme colour, like enamel cells.
        if (l < nearest) {
            nearest = l;
            step = float(i);
        }
        line = min(line, abs(l - shape.z - 0.5));
        total += l;
    }
    float body = clamp(exp(-nearest * 12.0) * 1.3, 0.0, 1.0);
    float lines = exp(-line * 30.0) * (0.7 + 0.6 * high);
    vec3 base = palette(view.y + step * 0.25 + nearest * 0.15 + total * 0.004);
    float kick = view.w;
    float b = min(1.0, body * 1.15 + lines * (0.55 + 0.6 * kick)) * view.z * (0.75 + 0.15 * level + 0.25 * pulse + 0.7 * kick);
    b = clamp(b, 0.0, 1.0);
    // Three tones, like a lit object: the shadow in the theme's blue sunk
    // into the window, the body in the palette, the lines in the accent.
    vec3 shadow = blend(window.rgb, c2.rgb, 0.45);
    vec3 colour = mix(window.rgb, shadow, smoothstep(0.0, 0.18, b));
    colour = blend(colour, base, smoothstep(0.08, 0.45, b));
    colour = blend(colour, c0.rgb, clamp(lines * (0.55 + 0.45 * kick), 0.0, 1.0) * smoothstep(0.3, 0.9, b));
    // Glints where a bright line crosses the body: the theme's foreground.
    float glint = clamp(lines * body * (0.6 + 0.8 * high + 0.6 * kick) - 0.35, 0.0, 1.0);
    return mix(colour, mix(c4.rgb, c5.rgb, 0.6), glint);
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
    // Four samples a pixel, on a rotated grid, averaged: one sample per big
    // pixel aliases the fractal's fine detail into speckle.
    vec2 o1 = vec2(0.125, 0.375) / side, o2 = vec2(0.375, -0.125) / side;
    vec3 colour = 0.25 * (seen(uv + o1, fold.x) + seen(uv - o1, fold.x) + seen(uv + o2, fold.x) + seen(uv - o2, fold.x));
    // The shockwave: a bright ring thrown out from the middle on each beat,
    // wobbling with the mirrors, fading as it goes.
    float kick = view.w;
    float wave = 0.30 + 0.62 * (1.0 - kick);
    float edge = abs(r - wave - 0.02 * sin(atan(uv.y, uv.x) * fold.x));
    colour = mix(colour, c4.rgb, clamp(kick * kick * smoothstep(0.06, 0.0, edge) * 0.9, 0.0, 1.0));
    // A new song's mirrors fade in over the last song's.
    if (fold.z < 1.0)
        colour = mix(0.5 * (seen(uv + o1, fold.y) + seen(uv - o1, fold.y)), colour, smoothstep(0.0, 1.0, fold.z));
    // Rim light: the accent around the edge, breathing with the beat.
    float rim = smoothstep(0.80, 0.90, r) * (1.0 - smoothstep(0.90, 0.99, r));
    colour = blend(colour, c0.rgb, rim * (0.25 + 0.55 * pulse + 0.4 * kick));
    float alpha = 1.0 - smoothstep(0.86, 1.0, r);
    fragColor = vec4(colour * alpha, alpha) * qt_Opacity;
}
