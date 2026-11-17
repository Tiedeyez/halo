#version 440

// The Mandelbrot visual: the set in square pixels, coloured from the Omarchy
// theme with a pearl's sheen, on an endless dive that only goes in. Where it
// looks is flown by MandelPilot (C++, double precision): `view` is the dive
// now, `next` the one a pixel dissolve is handing over to, `reveal` how far.
// The beat flashes the pearl (its thrust is the pilot's), the treble
// brightens the sheen, loudness sets the glow. Calm or paused, it holds.
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
    vec2 size;      // the grid, in pixels of the set (one fragment each)
    vec4 view;      // x, y, width across the shorter side, angle
    vec4 next;      // the dive being handed over to
    float reveal;   // 0..1 of the pixels showing `next`
    vec4 c0;        // accent
    vec4 c1;        // cyan
    vec4 c2;        // blue
    vec4 c3;        // magenta
    vec4 c4;        // bright cyan
    vec4 c5;        // foreground
    vec4 window;    // the window colour, inside the set
};

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
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
    // This is drawn into a small texture, one fragment per pixel of the
    // set; shaders/pixelgrid.frag enlarges it with the gaps between pixels.
    // Computing per screen pixel did the same sum 30 to 50 times over and
    // held the laptop's GPU near its top clock.
    vec2 id = floor(qt_TexCoord0 * size);

    // Centre of this pixel, -0.5..0.5 across the shorter side.
    float side = min(size.x, size.y);
    vec2 uv = (id + 0.5 - size * 0.5) / side;
    // A round window, its edge stepped in whole pixels.
    float r = length(uv) * 2.0;
    if (r > 1.0) {
        fragColor = vec4(0.0);
        return;
    }

    // Which dive this pixel shows: during a hand-over, pixels switch to the
    // new one in a random order.
    vec4 v = hash(id) < reveal ? next : view;
    float depth = log(2.4 / v.z);
    mat2 turn = mat2(cos(v.w), -sin(v.w), sin(v.w), cos(v.w));
    vec2 c = v.xy + turn * uv * v.z;

    // Escape time, with the derivative for the surface normal.
    int limit = int(90.0 + depth * 15.0);
    vec2 z = vec2(0.0);
    vec2 dz = vec2(0.0);
    int n = 0;
    float m2 = 0.0;
    for (int k = 0; k < 270; ++k) {
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
        float t = mu * 0.028 + time * 0.035;
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
    fragColor = vec4(colour * alpha, alpha) * qt_Opacity;
}
