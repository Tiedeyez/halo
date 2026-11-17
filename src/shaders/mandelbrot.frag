#version 440

// The Mandelbrot visual: the set in square pixels, coloured from the Omarchy
// theme with a pearl's sheen, on a slow voyage into one of four famous
// places and back out. The beat kicks the zoom and flashes the pearl; the
// bass turns it; the treble brightens the sheen; loudness sets the glow.
// Time stands still when calm or paused, so it holds a still image.
//
// Colour: smooth escape time picks a place on four of the theme's colours
// (a loop, so the bands never jump). The pearl is a soft thin-film iridescence
// mixed in at a fifth, and a highlight lit from the set's own surface: the
// direction of z / z' is the normal of the potential field.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;     // seconds of music heard
    float pulse;    // 0..1, the beat
    float level;    // 0..1, loudness
    float low;      // 0..1, bass
    float high;     // 0..1, treble
    float cell;     // pixel size in screen pixels
    vec2 size;      // item size in screen pixels
    vec4 c0;        // accent
    vec4 c1;        // cyan
    vec4 c2;        // blue
    vec4 c3;        // magenta
    vec4 c4;        // bright cyan
    vec4 c5;        // foreground
    vec4 window;    // the window colour, inside the set
};

const float Voyage = 40.0;   // seconds in and back out
const float Deepest = 8.2;   // e^8.2, about 3600x: float precision holds

vec2 target(int i) {
    if (i == 0) return vec2(-0.743643887, 0.131825904);   // Seahorse Valley
    if (i == 1) return vec2(0.2549870, 0.0005000);        // Elephant Valley
    if (i == 2) return vec2(-0.0452407411, 0.9868162204); // a triple spiral
    return vec2(-1.7686, -0.0017);                        // a mini-brot on the needle
}

vec3 palette(float t) {
    // The theme's accent, cyan, blue and magenta in a loop, eased between.
    // The light ones (bright cyan, foreground) are kept for the sheen, or a
    // theme with a near-white foreground washes the set out.
    t = fract(t) * 4.0;
    int i = int(floor(t));
    float f = smoothstep(0.0, 1.0, fract(t));
    vec3 a = i == 0 ? c0.rgb : i == 1 ? c1.rgb : i == 2 ? c2.rgb : c3.rgb;
    vec3 b = i == 0 ? c1.rgb : i == 1 ? c2.rgb : i == 2 ? c3.rgb : c0.rgb;
    return mix(a, b, f);
}

void main() {
    vec2 px = qt_TexCoord0 * size;
    vec2 id = floor(px / cell);
    vec2 inCell = fract(px / cell);
    float gap = 1.0 / cell;
    float inside = step(gap, inCell.x) * step(gap, inCell.y);

    // Centre of this pixel, -0.5..0.5 across the shorter side.
    float side = min(size.x, size.y);
    vec2 uv = ((id + 0.5) * cell - size * 0.5) / side;
    // A round window, its edge stepped in whole pixels.
    float r = length(uv) * 2.0;
    if (r > 1.0) {
        fragColor = vec4(0.0);
        return;
    }

    // The voyage: out at home, in to one place, out again, then the next.
    float voyage = time / Voyage;
    int place = int(mod(floor(voyage), 4.0));
    float phase = fract(voyage);
    float depth = Deepest * smoothstep(0.0, 1.0, 1.0 - abs(phase * 2.0 - 1.0));
    float zoom = exp(depth);
    vec2 home = vec2(-0.6, 0.0);
    vec2 centre = mix(home, target(place), 1.0 - 1.0 / zoom);
    float scale = 2.7 / zoom * (1.0 - 0.05 * pulse);
    float angle = time * 0.025 + low * 0.25;
    mat2 turn = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
    vec2 c = centre + turn * uv * scale;

    // Escape time, with the derivative for the surface normal.
    int limit = int(90.0 + depth * 14.0);
    vec2 z = vec2(0.0);
    vec2 dz = vec2(0.0);
    int n = 0;
    float m2 = 0.0;
    for (int k = 0; k < 220; ++k) {
        if (k >= limit) break;
        dz = 2.0 * vec2(z.x * dz.x - z.y * dz.y, z.x * dz.y + z.y * dz.x) + vec2(1.0, 0.0);
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        m2 = dot(z, z);
        if (m2 > 256.0) break;
        n = k + 1;
    }

    vec3 colour;
    float alpha = 1.0;
    if (m2 <= 256.0) {
        // Inside the set: the window colour, with a breath of the accent on
        // the beat.
        colour = mix(window.rgb, c0.rgb, 0.04 + 0.10 * pulse);
    } else {
        float mu = float(n) + 1.0 - log2(log(sqrt(m2)));
        float t = mu * 0.028 + time * 0.035 + pulse * 0.12;
        vec3 base = palette(t);

        // The pearl: a normal from z / z', lit from the upper left.
        vec2 u = z * vec2(dz.x, -dz.y) / max(dot(dz, dz), 1e-20);   // z / z'
        vec2 normal = normalize(u);
        vec2 light = normalize(vec2(-0.6, 0.8));
        float facing = clamp(dot(normal, light) * 0.5 + 0.5, 0.0, 1.0);
        vec3 film = 0.5 + 0.5 * cos(6.28318 * (facing * 0.7 + t * 0.5 + vec3(0.0, 0.33, 0.67)));
        // The film tints the theme colour rather than whitening it, so the
        // pearl stays in the theme.
        vec3 pearl = mix(base, base * (0.55 + 0.9 * film), 0.45);
        float sheen = pow(facing, 8.0) * (0.45 + 0.55 * high);
        colour = pearl * (0.6 + 0.5 * facing) + mix(c5.rgb, c4.rgb, 0.5) * sheen * 0.7;

        // Far from the set it fades toward the window, so the edges glow.
        float glow = clamp(mu / 24.0, 0.0, 1.0);
        colour = mix(window.rgb, colour, 0.45 + 0.55 * glow);
    }

    colour *= 0.6 + 0.3 * level + 0.2 * pulse;
    colour = min(colour, vec3(0.95));
    // The rim of the window fades in whole pixels.
    alpha *= 1.0 - smoothstep(0.86, 1.0, r);
    colour = mix(window.rgb * 0.6, colour, inside);
    fragColor = vec4(colour * alpha, alpha) * qt_Opacity;
}
