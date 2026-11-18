#version 440

// Halo's window background: pixelated water, and a second visualiser. A field
// of square pixels, mostly dark, crossed by drifting caustics, whose surface
// ripples hard to the music: every beat throws a drop from the cover (and
// more land elsewhere), the bass pumps rings out of the cover without pause,
// the mids make every ring wobble, and clicks and the pointer's wake drop
// ripples too. The rings spread, cross and fade, and bend the stars and rain
// beneath them. Time held still (calm), it is a still pond.
//
// Two ceilings: the steady water (caustics, stars, rain) stays dim enough
// that the faintest text over it reads at 7:1; the ripples flash brighter
// (hard enough to read as a visualiser), dipping text under a crest to
// about 3.6:1 for the moment it passes.
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
    float low;       // 0..1, bass: pumps rings out of the cover
    float mid;       // 0..1, mids: how much the rings wobble
    float strength;  // brightest the steady water gets, 0..1
    float flash;     // brightest a ripple gets, 0..1
    vec2 size;       // the grid, in cells
    vec2 origin;     // the cover, in cells
    vec4 accent;
    vec4 shine;      // a lighter colour for the brightest glints
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

// One drop's ripples at p: a packet of rings travelling out, widening as it
// goes and fading with age and distance. Their outline wobbles with the mids.
float ripple(vec2 p, vec4 d) {
    float age = time - d.z;
    if (d.w <= 0.0 || age < 0.0 || age > 6.0)
        return 0.0;
    vec2 v = p - d.xy;
    float r = length(v);
    float a = atan(v.y, v.x);
    // Uneven lobes, each turning its own way, so rings wobble like water
    // rather than forming stars.
    r += (sin(a * 3.0 + time * 2.3 + d.z * 7.0) * (0.4 + 1.8 * mid)
        + sin(a * 7.0 - time * 1.7 + d.z * 3.0) * (0.3 + 1.0 * mid)
        + sin(a * 2.0 + time * 0.9 + d.z * 5.0) * (0.3 + 1.2 * mid)) * min(1.0, age * 2.0);
    float speed = 22.0;
    float front = speed * age;
    float spread = 3.0 + age * 4.0;
    float x = r - front;
    float envelope = exp(-(x * x) / (spread * spread)) * exp(-age * 0.55) / sqrt(1.0 + r * 0.05);
    return d.w * envelope * smoothstep(0.0, 0.08, age) * sin(x * 0.7);
}

float waves(vec2 p) {
    float h = ripple(p, d0) + ripple(p, d1) + ripple(p, d2) + ripple(p, d3)
            + ripple(p, d4) + ripple(p, d5) + ripple(p, d6) + ripple(p, d7);
    // The bass pumps rings out of the cover all the time, wobbling too.
    vec2 v = p - origin;
    float r = length(v);
    r += sin(atan(v.y, v.x) * 4.0 + time * 1.3) * (1.0 + 3.0 * mid);
    h += low * low * 0.9 * sin(r * 0.30 - time * 6.5) * exp(-r / 70.0);
    return h;
}

float swell(vec2 p) {
    // A slow swell, heaving with the bass.
    float s = 0.10 * sin(p.x * 0.19 + time * 0.8)
            + 0.08 * sin(p.y * 0.23 - time * 0.6 + p.x * 0.07)
            + 0.06 * sin((p.x - p.y) * 0.11 + time * 1.1);
    return s * (0.3 + 0.7 * level + 1.2 * low);
}

float height(vec2 p) { return waves(p) + swell(p); }

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

float steps(float b) { return floor(clamp(b, 0.0, 1.0) * 4.0 + 0.5) / 4.0; }

void main() {
    vec2 id = floor(qt_TexCoord0 * size);
    vec2 p = id + 0.5;

    // The surface's slope, from its height either side.
    float hx = height(p + vec2(1.0, 0.0)) - height(p - vec2(1.0, 0.0));
    float hy = height(p + vec2(0.0, 1.0)) - height(p - vec2(0.0, 1.0));
    vec2 slope = vec2(hx, hy) * 0.5;

    // The ripples: wave tops glow all the way round, and the side facing the
    // light (from the upper left) glints.
    float w = waves(p);
    float crest = smoothstep(0.08, 0.40, w);
    float glint = clamp(dot(slope, normalize(vec2(-0.6, -0.8))) * 4.0, 0.0, 1.0);
    float wave = steps(max(crest * 0.75, glint));

    // The steady water: caustics, the drifting lines of light a pool throws,
    // and what's under the surface, bent by its slope.
    vec2 q = p + slope * 3.0;
    float w1 = sin(q.x * 0.31 + time * 0.9 + sin(q.y * 0.19 + time * 0.5) * 2.2);
    float w2 = sin(q.y * 0.27 - time * 0.7 + sin(q.x * 0.15 - time * 0.4) * 2.2);
    float caustic = smoothstep(0.84, 0.98, 0.5 + 0.5 * w1 * w2) * 0.45 * (0.4 + 0.6 * level);
    float seen = under(id + floor(slope * 10.0 + 0.5));
    float still = steps(max(caustic, seen));

    float a = max(still * strength, wave * flash);
    // The brightest glints go lighter, like light off wet glass.
    vec3 colour = mix(accent.rgb, shine.rgb, step(0.99, glint) * 0.45);
    fragColor = vec4(colour * a, a) * qt_Opacity;
}
