#version 440

// Halo's window background: pixelated water. A field of square pixels, mostly
// dark, crossed by drifting caustics, whose surface ripples: every drop (a beat, a click, the pointer's
// wake) sends out a ring of waves that spreads, crosses the others and dies
// away; the crests catch the light. Under the surface a few pixels glow like
// stars and thin columns rain down, both bent as the ripples pass over them.
// Kept dim so text over it keeps its contrast. Time held still (calm), it
// is a still pond.
//
// Drawn into a small texture, one fragment per pixel of the field;
// shaders/pixelgrid.frag enlarges it with the gaps between pixels.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;      // seconds
    float level;     // 0..1, loudness: the swell under everything
    float strength;  // brightest a pixel gets, 0..1
    vec2 size;       // the grid, in cells
    vec4 accent;
    // Up to eight drops: x, y (cells), when (seconds of `time`), how hard.
    vec4 d0;
    vec4 d1;
    vec4 d2;
    vec4 d3;
    vec4 d4;
    vec4 d5;
    vec4 d6;
    vec4 d7;
};

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// One drop's ripples at p: a packet of rings travelling out at `speed`
// cells a second, widening as it goes and fading with age and distance.
float ripple(vec2 p, vec4 d) {
    float age = time - d.z;
    if (d.w <= 0.0 || age < 0.0 || age > 9.0)
        return 0.0;
    float r = length(p - d.xy);
    float speed = 15.0;
    float front = speed * age;
    float spread = 2.5 + age * 3.5;
    float x = r - front;
    float envelope = exp(-(x * x) / (spread * spread)) * exp(-age * 0.42) / sqrt(1.0 + r * 0.08);
    // The fade-in over the first moment keeps the drop itself from popping.
    return d.w * envelope * smoothstep(0.0, 0.12, age) * sin(x * 0.85);
}

float height(vec2 p) {
    float h = ripple(p, d0) + ripple(p, d1) + ripple(p, d2) + ripple(p, d3)
            + ripple(p, d4) + ripple(p, d5) + ripple(p, d6) + ripple(p, d7);
    // A slow swell, stronger on louder music.
    float swell = 0.10 * sin(p.x * 0.19 + time * 0.8)
                + 0.08 * sin(p.y * 0.23 - time * 0.6 + p.x * 0.07)
                + 0.06 * sin((p.x - p.y) * 0.11 + time * 1.1);
    return h + swell * (0.25 + 0.75 * level);
}

float under(vec2 id) {
    // Stars: one pixel in thirty, each breathing at its own pace.
    float h = hash(id);
    float star = step(0.967, h) * (0.35 + 0.65 * (0.5 + 0.5 * sin(time * (0.4 + h * 1.6) + h * 40.0)));
    // Rain: a few columns carry a falling head with a fading tail.
    float column = hash(vec2(id.x, 7.0));
    float rain = 0.0;
    if (column > 0.93) {
        float speed = 4.0 + column * 10.0;
        float head = mod(time * speed + column * 300.0, size.y + 30.0);
        float behind = head - id.y;
        if (behind >= 0.0 && behind < 14.0)
            rain = (1.0 - behind / 14.0) * (behind < 1.0 ? 1.0 : 0.55);
    }
    return max(star * 0.7, rain);
}

void main() {
    vec2 id = floor(qt_TexCoord0 * size);
    vec2 p = id + 0.5;

    // The surface's slope, from its height either side.
    float hx = height(p + vec2(1.0, 0.0)) - height(p - vec2(1.0, 0.0));
    float hy = height(p + vec2(0.0, 1.0)) - height(p - vec2(0.0, 1.0));
    vec2 slope = vec2(hx, hy) * 0.5;

    // Wave tops glow all the way round; the side of each facing the light
    // (from the upper left) shines brighter.
    float h = height(p);
    float crest = smoothstep(0.12, 0.45, h) * 0.6;
    float glint = clamp(dot(slope, normalize(vec2(-0.6, -0.8))) * 3.2, 0.0, 1.0);
    float lit = max(crest, glint);
    // Caustics: the drifting lines of light a pool's surface throws, so the
    // water lives between drops. Louder music, brighter light.
    float w1 = sin(p.x * 0.31 + time * 0.9 + sin(p.y * 0.19 + time * 0.5) * 2.2);
    float w2 = sin(p.y * 0.27 - time * 0.7 + sin(p.x * 0.15 - time * 0.4) * 2.2);
    float caustic = smoothstep(0.84, 0.98, 0.5 + 0.5 * w1 * w2) * 0.45 * (0.4 + 0.6 * level);
    lit = max(lit, caustic);
    // What's under the water is seen bent by the slope.
    float seen = under(id + floor(slope * 6.0 + 0.5));

    float b = clamp(max(seen, lit), 0.0, 1.0);
    // Four brightness steps, like block shades.
    b = floor(b * 4.0 + 0.5) / 4.0;
    float a = b * strength;
    fragColor = vec4(accent.rgb * a, a) * qt_Opacity;
}
