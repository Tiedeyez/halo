#version 440

// The Halo visual: a ring of light in 3D, seen through a perspective camera,
// with the music driving it through the springs in HaloLight.qml.
//
// Each pixel casts a ray from the camera; where it crosses the ring's plane
// gives its place on that plane, and from that, how near the ray passes to
// the ring. The ring is a tube of light: its glow falls off as a line light
// does (about 1/distance), so the near side is thicker and the far side
// recedes. Plasma flows round it, its brightness round the ring follows the
// spectrum (bass at the front), and two strands wind along it.
//
// Every beat sends out radiation: a wavefront racing across the ring's
// plane and a shell of light round the halo, both slowing as they spread
// and thinning (dimming as 1/radius, as energy spreads); and sparks flung
// outward along the plane, all carried by one `travel` that each kick
// pushes and drag slows. Light pours off the ring above and below, motes
// drift away from it, and a broad haze holds it in the dark.
//
// Two passes of this shader, screen-blended by the app: `sharp` (ring,
// sparks, motes) at full resolution and the soft light at a third. The tone
// map is 1 - exp(-x), so tone-mapping each pass and screening them is the
// same as tone-mapping their sum. Output is premultiplied light.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float sharp;       // 1: ring, sparks, motes; 0: the soft light
    float time;        // seconds, held while paused or calm
    float pulse;       // the beat, 0..1
    float level;       // loudness, 0..1
    float energy;      // the ring's brightness envelope (bass, loudness)
    float aspect;      // width / height
    float radius;      // the ring's radius at rest, as a fraction of the height
    float size;        // the ring's size on its spring, 1 at rest
    float tilt;        // the camera's height above the ring's plane, radians
    float roll;        // the camera's roll, radians
    float spin;        // the plasma's flow round the ring
    float travel;      // how far the sparks have been carried
    float pixel;       // one screen pixel, in ring radii
    vec2 centre;       // the ring's centre, 0..1 of the item
    vec4 beats;        // seconds since each of the last four beats
    vec4 hits;         // how hard each of them hit, 0..1
    vec4 bandsLo;      // the spectrum, 8 bands, bass first
    vec4 bandsHi;
    vec4 c0;           // palette: the ring
    vec4 c1;           // glow
    vec4 c2;           // light above
    vec4 c3;           // light below, radiation
};

const float PI = 3.14159265;
const float TAU = 6.28318531;
const float CAM = 7.0;     // the camera's distance, in ring radii

float band(float x) {
    float f = clamp(x, 0.0, 1.0) * 7.0;
    return bandsLo.x * max(0.0, 1.0 - abs(f - 0.0)) + bandsLo.y * max(0.0, 1.0 - abs(f - 1.0))
         + bandsLo.z * max(0.0, 1.0 - abs(f - 2.0)) + bandsLo.w * max(0.0, 1.0 - abs(f - 3.0))
         + bandsHi.x * max(0.0, 1.0 - abs(f - 4.0)) + bandsHi.y * max(0.0, 1.0 - abs(f - 5.0))
         + bandsHi.z * max(0.0, 1.0 - abs(f - 6.0)) + bandsHi.w * max(0.0, 1.0 - abs(f - 7.0));
}

// Hashes without sin() (Dave Hoskins), cheap on integrated GPUs.
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

// The beams' pattern across the fan, above (side 101) or below (side 0).
vec2 beamField(float fx, float side) {
    float beamA = noise1(fx * 9.0 + side + time * 0.07);
    float beamB = noise1(fx * 23.0 + side * 1.7 - time * 0.11);
    float beamC = noise1(fx * 61.0 + side * 2.3 + time * 0.05);
    return vec2(pow(beamA * 0.5 + beamB * 0.32 + beamC * 0.18, 2.2) * 2.4, beamB);
}

// Motes in one depth, drifting up above the ring and down below it. `p` is
// y down here. Each lives in a grid cell and is faded out before the cell's
// edge, so no cell shows.
float motes(vec2 p, float scale, float speed, float seed, float size_) {
    float dir = p.y < 0.0 ? 1.0 : -1.0;
    vec2 s = vec2(p.x, p.y + dir * time * speed) * scale;
    vec2 cell = floor(s);
    vec4 h = hash4(cell + seed);
    if (h.w < 0.8)
        return 0.0;
    vec2 at = 0.24 + 0.52 * h.xy + 0.06 * vec2(sin(time * (0.7 + h.z) + h.x * TAU), cos(time * (0.5 + h.y) + h.z * TAU));
    float d = length(fract(s) - at) / scale;
    float sz = size_ * (0.4 + 1.2 * h.z * h.z);
    float glow = min(4.0 * sz, 0.22 / scale);
    float m = exp(-(d * d) / (sz * sz)) + 0.22 * exp(-(d * d) / (glow * glow));
    vec2 f = fract(s);
    float edgeDist = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    m *= smoothstep(0.0, 0.07, edgeDist);
    float twinkle = 0.55 + 0.45 * sin(time * (2.0 + 5.0 * h.y) + h.x * TAU);
    return m * twinkle * (0.5 + 0.5 * h.w) * smoothstep(0.0, 0.06, abs(p.y));
}

// Sparks flung off the rim in one layer: `n` slots round the ring, about
// half lit; each rides out from the rim with `travel`, over `range` ring
// radii, fading as it goes. `u` is the point on the ring's plane, `sinE`
// how steeply the ray meets the plane (to foreshorten the depth axis).
float sparks(vec2 u, float rl, float a, float sinE, float n, float range, float speed, float seed, float sz) {
    float fa = (a + PI) / TAU * n;
    float cell = floor(fa);
    vec4 h = hash4(vec2(cell, seed));
    if (h.z < 0.5)
        return 0.0;
    float fromRim = mod(travel * speed + h.x * range, range);
    float r = size * 1.03 + fromRim;
    float ac = (cell + 0.25 + 0.5 * h.y) / n * TAU - PI;
    vec2 rhat = u / max(rl, 1e-4);
    vec2 that = vec2(-rhat.y, rhat.x);
    float across = rl - r;
    // A short trail behind it, toward the ring it came from.
    across *= across < 0.0 ? 0.28 : 1.0;
    vec2 o = rhat * across + that * (a - ac) * rl;
    float sl = length(vec2(o.x, o.y * sinE));
    float s = sz * (0.6 + 0.8 * h.w);
    float k = fromRim / range;
    float fade = pow(1.0 - k, 1.4) * smoothstep(0.0, 0.04, k);
    // Faded out before the slot's edges, so no slot shows.
    float edge = smoothstep(0.0, 0.12, min(fract(fa), 1.0 - fract(fa)));
    return exp(-(sl * sl) / (s * s)) * fade * edge;
}

void main() {
    vec2 uv = qt_TexCoord0;
    // Screen, in ring radii at the ring's distance: x right, y up.
    vec2 p = (uv - centre) * vec2(aspect, 1.0) / radius;
    p.y = -p.y;

    // ---- the camera: above the ring's plane by `tilt`, looking at its middle ----
    float se = sin(tilt), ce = cos(tilt);
    vec3 cam = CAM * vec3(0.0, se, ce);
    vec3 fwd = vec3(0.0, -se, -ce);
    vec3 right0 = vec3(1.0, 0.0, 0.0);
    vec3 up0 = vec3(0.0, ce, -se);
    vec3 right = cos(roll) * right0 + sin(roll) * up0;
    vec3 up = -sin(roll) * right0 + cos(roll) * up0;
    vec3 dir = normalize(fwd * CAM + right * p.x + up * p.y);

    // Where the ray crosses the ring's plane (y = 0); rays at or above the
    // horizon never do, and count as far away.
    float sinE = max(-dir.y, 1e-3);
    float t = cam.y / sinE;
    vec3 hit = cam + dir * t;
    vec2 u = hit.xz;                                 // +z: toward the camera
    float rl = length(u);
    float ringR = size;
    float radial = rl - ringR;
    // How near the ray passes the ring (ring radii): its distance across the
    // plane, foreshortened by how steeply the ray meets the plane.
    float d3 = abs(radial) * sinE;
    // Nearer the camera is brighter and thicker (perspective).
    float near = CAM / t;
    float depth = smoothstep(0.9, 1.1, near) * 0.6 + 0.55;

    // The spectrum round the ring, bass at the front. Near the middle, where
    // the angle means nothing, it is the overall level instead (no seam).
    float a = atan(u.y, u.x);
    float around = atan(abs(u.x), u.y) / PI;          // 0 front .. 1 back
    float spec = mix(0.35 + 0.5 * level, band(around), smoothstep(0.35, 0.8, rl / ringR));

    float glowE = 0.3 + 0.75 * energy + 0.6 * pulse;
    vec3 col = vec3(0.0);

    if (sharp > 0.5) {
        // ---- the ring: a tube of light ----
        float w = max(0.014 * ringR * (0.75 + 0.5 * energy + 0.4 * spec), pixel * 1.1 * t / CAM);
        if (d3 < 0.35) {
            // Plasma flowing round it.
            float flow = noiseLoop((a / TAU + 0.5) * 24.0 + spin * 1.3, 24.0)
                       * noiseLoop((a / TAU + 0.5) * 57.0 - spin * 2.1, 57.0);
            float plasma = 0.55 + 1.3 * flow;
            float core = exp(-(d3 * d3) / (w * w));
            float hot = exp(-(d3 * d3) / (w * w * 0.12));
            // Two strands winding along the tube.
            float strands = 0.0;
            for (int k = 0; k < 2; ++k) {
                float fk = float(k);
                float off = w * 1.7 * sin(a * (31.0 + fk * 12.0) + spin * (3.0 - fk * 5.0) + fk * 2.0);
                float ds = abs(radial - off) * sinE / (w * 0.28);
                strands += exp(-ds * ds) * 0.45;
            }
            // The tight glow round the tube: a line light, about 1/distance.
            float fade = smoothstep(0.35, 0.18, d3);
            float tube = (w * 2.2 / (d3 + w * 2.2)) * 0.42 * fade;
            float ring = core * (0.9 + 1.6 * spec) * plasma + hot * 1.6 + strands * (0.5 + spec) * plasma;
            vec3 hotCol = mix(c0.rgb, vec3(1.0), 0.7);
            vec3 tubeCol = mix(c0.rgb, c1.rgb, smoothstep(-1.0, 1.0, radial / (w * 3.0)));
            col += (ring * hotCol + tube * tubeCol * (0.6 + spec)) * depth * glowE;
        }

        // ---- sparks flung off the rim ----
        if (rl > ringR * 0.98 && rl < ringR + 2.4) {
            float sp = sparks(u, rl, a, sinE, 150.0, 1.6, 1.0, 3.0, 0.007)
                     + sparks(u, rl, a, sinE, 90.0, 2.3, 0.7, 11.0, 0.009) * 0.8;
            col += sp * mix(c0.rgb, vec3(1.0), 0.6) * (0.5 + 1.2 * energy) * depth;
        }

        // ---- motes ----
        float lift = exp(-abs(p.y) * 0.8) * smoothstep(1.9, 0.4, abs(p.x));
        if (lift > 0.01) {
            vec2 pm = vec2(p.x, -p.y);
            float dust = (motes(pm, 9.0, 0.10, 3.1, 0.006) * 0.55
                        + motes(pm, 17.0, 0.18, 7.7, 0.004) * 0.75) * lift * (0.5 + 0.8 * level);
            col += dust * mix(c0.rgb, vec3(1.0), 0.55);
        }
    } else {
        // ---- the ring's wide glow, holding it in the dark ----
        // Thinner inside, so it reads as a ring of light, not a disc.
        float inner = mix(0.35, 1.0, smoothstep(-0.25, 0.0, radial));
        float g = (1.0 / (1.0 + (d3 / 0.045) * (d3 / 0.045)) * 0.4 + exp(-d3 / 0.3) * 0.14) * inner;
        col += g * mix(c0.rgb, c1.rgb, 0.5) * depth * glowE * (0.7 + 0.6 * spec);

        // ---- radiation from each beat ----
        float rp = length(p);
        for (int k = 0; k < 4; ++k) {
            float age = k == 0 ? beats.x : k == 1 ? beats.y : k == 2 ? beats.z : beats.w;
            float hit_ = k == 0 ? hits.x : k == 1 ? hits.y : k == 2 ? hits.z : hits.w;
            if (age < 0.0 || age > 2.4 || hit_ <= 0.0)
                continue;
            // Fast out of the ring, slowing as it spreads.
            float R = ringR * (1.0 + 2.8 * (1.0 - exp(-age * 1.7)));
            float th = 0.025 + 0.11 * age;
            // Thinning as 1/R, and gone smoothly before it ends.
            float amp = hit_ * exp(-age * 1.2) * (2.6 / R) * smoothstep(2.4, 1.6, age);
            float dp = abs(rl - R) * sinE / th;
            float wavefront = exp(-dp * dp) + 0.25 * exp(-abs(rl - R) * sinE / (th * 4.0));
            float ds = (rp - R * 0.95) / (th * 1.6);
            float shell = exp(-ds * ds) * 0.55 + smoothstep(R * 0.95, 0.0, rp) * 0.04;
            col += (wavefront * mix(c3.rgb, vec3(1.0), 0.35) + shell * mix(c1.rgb, c3.rgb, 0.5)) * amp;
        }

        // ---- light pouring off the ring above and below ----
        // The ring's outline on screen, near enough an ellipse at this
        // distance: half width `ringR`, half height `ringR * sin(tilt)`.
        float ax = abs(p.x);
        float span = sqrt(max(0.0, 1.0 - min((ax / ringR) * (ax / ringR), 1.0)));
        float edge = ringR * se * span;
        float outside = smoothstep(-0.004, 0.02, radial);
        float upD = max(0.0, p.y - edge);
        float downD = max(0.0, -edge - p.y);
        float isUp = smoothstep(-0.1, 0.1, p.y);
        float dist = upD + downD;
        float column = smoothstep(1.08, 0.6, ax / ringR / (1.0 + 0.35 * dist)) * (0.3 + 0.7 * span) * outside;
        float fx = p.x / (1.0 + 0.55 * dist);
        float curtain = 0.0;
        float beams = 0.0;
        if (column > 0.001) {
            vec2 bf = isUp > 0.999 ? beamField(fx, 101.0)
                    : isUp < 0.001 ? beamField(fx, 0.0)
                    : mix(beamField(fx, 0.0), beamField(fx, 101.0), isUp);
            beams = bf.x;
            float colSpec = band(clamp(ax / ringR, 0.0, 1.0));
            float height = mix(0.4 + 0.5 * energy + 0.4 * colSpec, 0.55 + 0.75 * energy + 0.5 * colSpec, isUp);
            float packet = 0.5 + 0.5 * sin(dist * 9.0 - time * (2.2 + 2.5 * level) + bf.y * TAU);
            float body = exp(-dist / height);
            float lip = exp(-dist / 0.05) * (0.4 + 0.7 * spec) * glowE;
            // Each beat's surge: a band of light leaving the ring.
            float surges = 0.0;
            for (int k = 0; k < 4; ++k) {
                float age = k == 0 ? beats.x : k == 1 ? beats.y : k == 2 ? beats.z : beats.w;
                float hit_ = k == 0 ? hits.x : k == 1 ? hits.y : k == 2 ? hits.z : hits.w;
                if (age < 0.0 || age > 1.4)
                    continue;
                float x = (dist - age * 1.7) / (0.08 + age * 0.12);
                surges += exp(-x * x) * exp(-age * 2.2) * smoothstep(1.4, 0.9, age) * hit_;
            }
            curtain = column * (body * (0.12 + beams * (0.45 + 0.35 * packet + 1.6 * surges)) * glowE + lip);
        }
        col += curtain * isUp * mix(c2.rgb, vec3(1.0), 0.15 * beams) * 0.85;
        col += curtain * (1.0 - isUp) * mix(c3.rgb, vec3(1.0), 0.15 * beams) * 0.7;

        // ---- a broad haze, and the farthest motes (a little out of focus) ----
        col += exp(-p.x * p.x * 0.8) * exp(-abs(p.y) * 0.9) * (0.05 + 0.1 * energy) * mix(c3.rgb, c2.rgb, isUp);
        float lift = exp(-abs(p.y) * 0.8) * smoothstep(1.9, 0.4, ax);
        if (lift > 0.01)
            col += motes(vec2(p.x, -p.y), 31.0, 0.30, 1.3, 0.0036) * 0.6 * lift * (0.5 + 0.8 * level)
                 * mix(c0.rgb, vec3(1.0), 0.4);
    }

    // Tone map: highlights roll off to white instead of clipping.
    col = vec3(1.0) - exp(-col * 1.15);
    // Dither, under one step of 8-bit, against banding in the gradients.
    float n = hash4(gl_FragCoord.xy + fract(time) * 61.0).x - 0.5;
    col = max(col + n / 255.0, 0.0);
    float alpha = clamp(max(col.r, max(col.g, col.b)), 0.0, 1.0);
    fragColor = vec4(col, alpha) * qt_Opacity;
}
