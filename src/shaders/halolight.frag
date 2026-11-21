#version 440

// The Halo visual: a ring of light hanging in the dark, tilted toward the
// viewer, and the light it throws. Everything is computed per pixel (no
// textures), in two passes: the ring, motes and flare at the screen's full
// resolution, so they are as sharp on a 4K screen as on a laptop, and the
// soft light round them at half (the app scales it up). Layers, from the
// ring out:
//   - the ring itself: a white-hot core, braided filaments wound round it,
//     and comets of energy running along it; its thickness and brightness
//     follow the spectrum round its circumference (bass at the front,
//     highs at the back), and its far side is dimmer, as if seen in depth;
//   - its bloom, in three widths, and an anamorphic streak at its sides;
//   - rays, radiating from the ring in its own plane, each ray's length
//     and strength from the band of the spectrum at its angle;
//   - curtains of light rising above the ring and falling below it, made of
//     fine streaks that travel away from it, brighter with loudness;
//   - motes of light in three depths, drifting up above and down below,
//     twinkling with the treble;
//   - on every beat (up to four at once), a shockwave across the ring's
//     plane and a surge of light up and down the beams;
//   - a faint film of light inside the ring.
// Output is light: premultiplied, as opaque as it is bright, added over the
// dark window. A little dither keeps the long gradients free of banding.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;        // seconds, held while paused or calm
    float pulse;       // the beat, 0..1, peaks on the kick
    float level;       // loudness, 0..1
    float low;         // bass, 0..1
    float high;        // treble, 0..1
    float aspect;      // width / height
    float radius;      // the ring's radius, as a fraction of the height
    float tilt;        // the ring's height / width on screen
    float pixel;       // one screen pixel, in ring radii
    float sharp;       // 1: the sharp pass (ring, motes, flare) at full
                       // resolution; 0: the soft light, drawn at half
    vec2 centre;       // the ring's centre, 0..1 of the item
    vec4 beats;        // seconds since each of the last four beats
    vec4 bandsLo;      // the spectrum, 8 bands, bass first
    vec4 bandsHi;
    vec4 c0;           // palette: the ring
    vec4 c1;           // bloom and rays
    vec4 c2;           // the light above
    vec4 c3;           // the light below, shockwaves
};

const float PI = 3.14159265;
const float TAU = 6.28318531;

float band(float x) {
    // x 0..1 across the 8 bands, linearly between them (weights, not
    // indexing, so it compiles for GLSL ES 1.00 too).
    float f = clamp(x, 0.0, 1.0) * 7.0;
    return bandsLo.x * max(0.0, 1.0 - abs(f - 0.0)) + bandsLo.y * max(0.0, 1.0 - abs(f - 1.0))
         + bandsLo.z * max(0.0, 1.0 - abs(f - 2.0)) + bandsLo.w * max(0.0, 1.0 - abs(f - 3.0))
         + bandsHi.x * max(0.0, 1.0 - abs(f - 4.0)) + bandsHi.y * max(0.0, 1.0 - abs(f - 5.0))
         + bandsHi.z * max(0.0, 1.0 - abs(f - 6.0)) + bandsHi.w * max(0.0, 1.0 - abs(f - 7.0));
}

// Hashes without sin() (Dave Hoskins), much cheaper on integrated GPUs.
float hash1(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}
vec4 hash4(vec2 p) {
    vec4 q = fract(vec4(p.xyxy) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    q += dot(q, q.wzxy + 33.33);
    return fract((q.xxyz + q.yzzw) * q.zywx);
}
// Smooth 1D value noise that wraps every `period` (for angles).
float noiseLoop(float x, float period) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash1(mod(i, period)), hash1(mod(i + 1.0, period)), f);
}
float noise1(float x) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash1(i), hash1(i + 1.0), f);
}

// The beat's shockwave in the ring's plane, `age` seconds after the beat.
float shock(float lq, float age) {
    if (age < 0.0 || age > 1.6)
        return 0.0;
    float r = 1.0 + age * 1.9 - age * age * 0.25;
    float w = 0.02 + age * 0.08;
    float x = (lq - r) / w;
    // Fades to nothing before it ends, so it never vanishes with a step.
    return 1.8 * exp(-x * x) * exp(-age * 2.4) * (1.0 + 0.8 * exp(-age * 12.0)) * smoothstep(1.6, 1.1, age);
}

// The beat's surge up and down the beams: a band of light leaving the ring.
float surge(float dist, float age) {
    if (age < 0.0 || age > 1.4)
        return 0.0;
    float x = (dist - age * 1.7) / (0.08 + age * 0.12);
    return exp(-x * x) * exp(-age * 2.2) * smoothstep(1.4, 0.9, age);
}

// Motes in one depth: one per grid cell at most, drifting away from the ring.
float motes(vec2 p, float scale, float speed, float seed, float sizeK) {
    float dir = p.y < 0.0 ? 1.0 : -1.0;      // above: up the screen; below: down
    vec2 s = vec2(p.x, p.y + dir * time * speed) * scale;
    vec2 cell = floor(s);
    vec4 h = hash4(cell + seed);
    if (h.w < 0.8)
        return 0.0;
    vec2 at = 0.15 + 0.7 * h.xy + 0.08 * vec2(sin(time * (0.7 + h.z) + h.x * TAU), cos(time * (0.5 + h.y) + h.z * TAU));
    float d = length(fract(s) - at) / scale;             // in ring radii
    float size = sizeK * (0.4 + 1.2 * h.z * h.z) * (1.0 + 0.8 * high);
    float twinkle = 0.55 + 0.45 * sin(time * (2.0 + 5.0 * h.y) + h.x * TAU);
    float m = exp(-(d * d) / (size * size)) + 0.12 * size / (d + size);
    // A small four-point star on the largest.
    if (h.z > 0.8) {
        vec2 o = (fract(s) - at) / scale;
        m += 0.35 * exp(-abs(o.x) / (size * 0.35)) * exp(-abs(o.y) / (size * 5.0))
           + 0.35 * exp(-abs(o.y) / (size * 0.35)) * exp(-abs(o.x) / (size * 5.0));
    }
    return m * twinkle * (0.5 + 0.5 * h.w);
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 p = (uv - centre) * vec2(aspect, 1.0) / radius;   // ring radius = 1, y down
    float energy = 0.7 + 0.5 * level + 0.8 * pulse;

    // ---- the ring's geometry: an ellipse, the circle tilted toward us ----
    vec2 q = vec2(p.x, p.y / tilt);                // in the ring's plane
    float lq = length(q);
    float theta = atan(q.y, q.x);                  // +: the front (lower) half
    vec2 grad = vec2(q.x, q.y / tilt) / max(lq, 1e-4);
    float d = (lq - 1.0) / max(length(grad), 1e-3);  // signed screen distance, ring radii
    float ad = abs(d);
    float front = 0.5 + 0.5 * sin(theta);          // 1 nearest the viewer
    float depth = mix(0.5, 1.0, smoothstep(0.0, 1.0, front));

    // The spectrum round the ring, mirrored left to right: bass at the front.
    float around = atan(abs(q.x), q.y) / PI;       // 0 front .. 1 back
    float spec = band(around);

    // Two passes of the same shader, screen-blended by the app: the tone map
    // below is 1 - exp(-x), so tone-mapping each pass and screening them is
    // the same as tone-mapping their sum.
    vec3 col = vec3(0.0);
    if (sharp > 0.5) {
        // ---- the ring: core, filaments, comets ----
        float w = max(pixel * 1.2, 0.005 + 0.012 * spec + 0.006 * pulse);
        float core = exp(-(d * d) / (w * w));
        float hotCore = exp(-(d * d) / (w * w * 0.12));

        // The ring's detail only matters near it: skip it elsewhere (most of the
        // window), which keeps the visual at full frame rate on integrated GPUs.
        float strands = 0.0;
        float comets = 0.0;
        float b1 = 0.0;
        float corona = 0.0;
        if (ad < 0.25) {
            for (int k = 0; k < 3; ++k) {
                float fk = float(k);
                float n = 23.0 + fk * 14.0;
                float amp = w * (0.7 + fk * 0.35) * (0.8 + 0.5 * spec);
                float off = amp * sin(theta * n + time * (1.3 - fk * 0.55) * (k == 1 ? -1.0 : 1.0) + fk * 2.1);
                float sw = max(pixel * 0.8, w * 0.18);
                float x = (d - off) / sw;
                strands += exp(-x * x) * (0.4 - fk * 0.1);
            }

            for (int k = 0; k < 4; ++k) {
                float fk = float(k);
                float dir = mod(fk, 2.0) < 0.5 ? 1.0 : -1.0;
                float a = theta - dir * time * (0.55 + 0.23 * fk) - fk * 1.7;
                // A bright head and a long tail behind it.
                float ph = mod(a, TAU);
                float head = exp(-pow(min(ph, TAU - ph) * 9.0, 2.0));
                float tail = exp(-mod(dir * a, TAU) * 2.2) * 0.45;
                comets += (head + tail) * (0.6 + 0.8 * spec);
            }
            // Faded to nothing at the edge of this region, so it leaves no seam.
            float fade = smoothstep(0.25, 0.12, ad);
            b1 = fade / (1.0 + (d * d) / 0.0009);
            comets *= fade;
            float cx = (lq - 1.09 - 0.03 * pulse) / 0.012;
            corona = exp(-cx * cx) * (0.12 + 0.3 * spec);
        }
        float ringLight = core * (1.0 + 2.2 * spec) + hotCore * 1.4 + strands * (0.6 + 1.4 * spec);
        ringLight += comets * (core * 1.6 + exp(-ad / (w * 4.0)) * 0.5);

        // The tight bloom and the corona hug the ring: sharp.
        float bloomSharp = corona + b1 * (0.5 + 0.9 * spec);

        // An anamorphic streak through the ring's sides.
        float flare = exp(-abs(p.y) / (0.0035 + 0.004 * pulse)) * exp(-abs(abs(p.x) - 1.0) * 1.6)
                    * (0.25 + 0.6 * pulse + 0.25 * band(0.0));

        // ---- motes ----
        float lift = exp(-abs(p.y) * 0.8) * smoothstep(1.9, 0.4, abs(p.x));
        float dust = 0.0;
        if (lift > 0.01)
            dust = (motes(p, 9.0, 0.10, 3.1, 0.006) * 0.55
                    + motes(p, 17.0, 0.18, 7.7, 0.004) * 0.75
                    + motes(p, 31.0, 0.30, 1.3, 0.0028) * 0.6) * lift * (0.6 + 0.8 * level);

        vec3 hot = mix(c0.rgb, vec3(1.0), 0.65);
        col += (ringLight * hot * 1.4 + bloomSharp * mix(c0.rgb, c1.rgb, 0.5)) * depth;
        col += flare * mix(c1.rgb, vec3(1.0), 0.3);
        col += dust * mix(c0.rgb, vec3(1.0), 0.55);
    } else {
        // ---- the wide bloom ----
        float b2 = 1.0 / (1.0 + (d * d) / 0.006);
        float b3 = exp(-ad / (0.3 + 0.25 * level));
        float bloom = b2 * (0.22 + 0.35 * level) + b3 * (0.10 + 0.18 * pulse);

        // ---- rays, radiating in the ring's plane ----
        float u = theta / TAU;
        float r1 = noiseLoop(u * 180.0 + time * 0.8, 180.0);
        float r2 = noiseLoop(u * 67.0 - time * 0.35, 67.0);
        float r3 = noiseLoop(u * 23.0 + time * 0.12, 23.0);
        float r4 = noiseLoop(u * 331.0 - time * 1.4, 331.0);
        float rayShape = pow(r1 * 0.35 + r2 * 0.3 + r3 * 0.15 + r4 * 0.2, 4.0) * 1.9;
        // Mostly up and down: the light pours off the ring's top and bottom.
        float vertical = mix(0.3, 1.0, pow(abs(q.y) / max(lq, 1e-3), 0.8));
        rayShape *= vertical;
        float reach = 0.14 + 0.45 * spec + 0.22 * level;
        float outward = max(d, 0.0);
        float inward = max(-d, 0.0);
        float rays = rayShape * (exp(-outward / reach) * smoothstep(0.0, 0.02, outward)
                               + 0.35 * exp(-inward / (reach * 0.35)));
        // Fine shimmer travelling out along the rays.
        // Packets of light travelling out along the rays.
        rays *= 0.82 + 0.18 * sin(lq * 13.0 - time * 3.5 + r2 * TAU);

        // ---- curtains of light above and below ----
        // Beams pour off the ring's top edge (up) and bottom edge (down). They
        // begin at the ring itself, never inside it, and fan out as they go, so
        // the light reads as thrown by the ring, not hung behind it.
        float ax = abs(p.x);
        float span = sqrt(max(0.0, 1.0 - min(ax * ax, 1.0)));
        float edge = tilt * span;
        float outside = smoothstep(-0.004, 0.02, d);   // 0 inside the ring
        float upD = max(0.0, -edge - p.y);             // distance above the ring's top
        float downD = max(0.0, p.y - edge);            // below its bottom
        float isUp = step(p.y, 0.0);
        float dist = mix(downD, upD, isUp);
        float column = smoothstep(1.08, 0.6, ax / (1.0 + 0.35 * dist)) * (0.3 + 0.7 * span) * outside;
        // Fan: the beams' horizontal coordinate widens with distance.
        float fx = p.x / (1.0 + 0.55 * dist);
        float side = isUp * 101.0;                     // different beams above and below
        float curtain = 0.0;
        float beams = 0.0;
        if (column > 0.001) {
            float beamA = noise1(fx * 9.0 + side + time * 0.07);
            float beamB = noise1(fx * 23.0 + side * 1.7 - time * 0.11);
            float beamC = noise1(fx * 61.0 + side * 2.3 + time * 0.05);
            beams = pow(beamA * 0.5 + beamB * 0.32 + beamC * 0.18, 2.2) * 2.4;
            float colSpec = band(clamp(ax, 0.0, 1.0));     // bass in the middle, highs at the sides
            float height = mix(0.4 + 0.6 * level + 0.5 * colSpec, 0.55 + 0.85 * level + 0.6 * colSpec, isUp)
                         + 0.5 * pulse;
            // Packets of light travelling away from the ring, quicker on loud parts.
            float packet = 0.5 + 0.5 * sin(dist * 9.0 - time * (2.2 + 2.5 * level) + beamB * TAU);
            float body = exp(-dist / height);
            float lip = exp(-dist / 0.05) * (0.5 + 0.8 * spec) * (1.0 + 1.5 * pulse);   // the edge it leaves from
            float surges = surge(dist, beats.x) + surge(dist, beats.y) + surge(dist, beats.z) + surge(dist, beats.w);
            curtain = column * (body * (0.14 + beams * (0.6 + 0.4 * packet + 1.4 * surges)) + lip);
        }
        float above = curtain * isUp;
        float below = curtain * (1.0 - isUp) * 0.85;
        float streak = beams;
        // A broad, soft glow behind the beams.
        float haze = exp(-ax * ax * 1.2) * exp(-abs(p.y) * 1.1) * (0.07 + 0.12 * level + 0.1 * pulse);

        // ---- beat shockwaves, and the film inside the ring ----
        float waves = shock(lq, beats.x) + shock(lq, beats.y) + shock(lq, beats.z) + shock(lq, beats.w);
        float film = smoothstep(1.0, 0.2, lq) * (0.05 + 0.08 * level)
                   * (0.7 + 0.3 * sin(lq * 22.0 - time * 1.7 + theta * 3.0));

        col += bloom * mix(c0.rgb, c1.rgb, 0.5) * depth;
        col += rays * mix(c1.rgb, c0.rgb, r1) * 0.5;
        col += above * mix(c2.rgb, vec3(1.0), 0.15 * streak) * 0.9;
        col += below * mix(c3.rgb, vec3(1.0), 0.15 * streak) * 0.8;
        col += haze * mix(c1.rgb, c2.rgb, step(0.0, -p.y));
        col += waves * mix(c3.rgb, vec3(1.0), 0.35) * depth * 1.3;
        col += film * c1.rgb;
    }
    col *= energy;

    // Tone map: highlights roll off to white instead of clipping.
    col = vec3(1.0) - exp(-col * 1.15);
    // Dither, under one step of 8-bit, against banding in the gradients.
    float n = hash4(gl_FragCoord.xy + fract(time) * 61.0).x - 0.5;
    col = max(col + n / 255.0, 0.0);
    float a = clamp(max(col.r, max(col.g, col.b)), 0.0, 1.0);
    fragColor = vec4(col, a) * qt_Opacity;
}
