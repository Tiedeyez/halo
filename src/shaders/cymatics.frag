#version 440

// Cymatics: sand on a plate the music shakes (Chladni's figures). The plate
// rings in a standing wave,
//     f = cos(n pi x) cos(m pi y) + s cos(m pi x) cos(n pi y),
// and stands still along the lines where f = 0; the sand collects there.
// CymaticsPilot (src/cymatics.h) picks n, m and s from the music's pitch and
// changes them on the beat; here two figures are mixed by `morph`, so the
// lines travel to their new places while the sand is thrown about
// (`agitation`) and settles again.
//
// A cell holds sand when it is near a line: how near is |f| over the
// slope of f, which is the distance to the line in cells, so lines keep one
// width whatever the figure. Each cell has its own grain, which roughens
// the lines' edges like sand; loose grains lie between the lines when the
// music is quiet and jump while the figure changes. A click on the plate
// throws the sand round it (`tap`).
//
// Two looks from one shader. As the window's background (`vivid` 0) the
// plate is the whole window, repeating as a plate's figure does, in one
// colour and dim enough that the faintest text over it reads at 7:1
// (`strength`), lifting a little on the beat (`flash`). As the visual
// (`vivid` 1) it is one square plate: bright sand, and under it the plate's
// own motion, the two halves of the wave glowing in turn in two colours.
//
// Drawn into a small texture, one fragment per cell; pixelgrid.frag or
// ledgrid.frag enlarges it into square screen pixels.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;       // seconds, held while still
    float morph;      // 0 = modeA, 1 = modeB
    float agitation;  // 0..1, sand in the air
    float gather;     // 0..1, how hard the sand is driven onto the lines
    float kick;       // 1 on a beat, falling fast
    float low;        // 0..1, bass
    float strength;   // background: brightest the steady sand gets
    float flash;      // background: brightest it gets on a beat
    float vivid;      // 0 = background, 1 = the visual
    float unit;       // cells across one plate
    vec2 size;        // the grid, in cells
    vec2 centre;      // the plate's centre, in cells
    vec4 modeA;       // n, m, sign
    vec4 modeB;
    vec4 tap;         // x, y (cells), when (seconds of `time`), how hard
    vec4 accent;
    vec4 shine;       // a lighter colour for the thickest sand
    vec4 c2;          // the visual: the wave's rising half
    vec4 c3;          // ...and its falling half
};

const float PI = 3.14159265;

// No visible rows or columns, which a cheaper hash leaves in sparse grains.
float hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// A figure's height in x and its slope in yz, at p (0..1 across the plate).
vec3 figure(vec2 p, vec4 mode) {
    vec2 a = mode.xy * PI;                    // n pi, m pi
    float cnx = cos(a.x * p.x), snx = sin(a.x * p.x);
    float cmx = cos(a.y * p.x), smx = sin(a.y * p.x);
    float cny = cos(a.x * p.y), sny = sin(a.x * p.y);
    float cmy = cos(a.y * p.y), smy = sin(a.y * p.y);
    float f = cnx * cmy + mode.z * cmx * cny;
    float fx = -a.x * snx * cmy - mode.z * a.y * smx * cny;
    float fy = -a.y * cnx * smy - mode.z * a.x * cmx * sny;
    return vec3(f, fx, fy);
}

float steps(float b) { return floor(clamp(b, 0.0, 1.0) * 4.0 + 0.5) / 4.0; }

void main() {
    vec2 id = floor(qt_TexCoord0 * size);
    vec2 cellAt = id + 0.5;
    // The beat makes the visual's plate jump a hair toward the eye.
    float span = unit * (1.0 + 0.012 * kick * vivid);
    vec2 p = (cellAt - centre) / span + 0.5;

    float mixing = smoothstep(0.0, 1.0, morph);
    vec3 w = mix(figure(p, modeA), figure(p, modeB), mixing);
    float f = w.x;

    // A click throws the sand round it, in a ring that widens and dies.
    float age = time - tap.z;
    float thrown = 0.0;
    if (tap.w > 0.0 && age >= 0.0 && age < 2.5) {
        float r = length(cellAt - tap.xy);
        float reach = 6.0 + age * 55.0;
        thrown = tap.w * exp(-age * 2.2) * exp(-(r * r) / (reach * reach));
    }
    float air = clamp(agitation + thrown, 0.0, 1.0);

    // Grains: one fixed per cell, and one that changes many times a second
    // while sand is in the air (held still otherwise, so a still plate is
    // still).
    float grain = hash(id);
    float hop = hash(id + floor(time * 24.0) * step(0.02, air) * vec2(17.0, 31.0) + 3.7);

    // Distance to the nearest line, in cells. The small constant keeps the
    // saddles (flat and at zero) from swallowing whole patches.
    float away = abs(f) * span / (length(w.yz) + 1.2);
    away += (hop - 0.5) * air * 5.0 + (grain - 0.5) * 0.9;
    float width = mix(2.5, 1.4, gather) + air * 1.6;
    float line = 1.0 - smoothstep(width * 0.45, width * 1.35, abs(away));
    // Sand, not ink: the grain eats into the line's edge.
    float sand = line * (0.62 + 0.38 * grain) * step(grain * 0.55, line);

    // Loose grains between the lines: a few when quiet, many in the air.
    float loose = (1.0 - gather) * 0.035 + air * 0.16;
    float scattered = step(1.0 - loose, hop) * (0.35 + 0.45 * air);
    sand = max(sand, scattered);

    // Under the sand the plate itself moves: where the wave is high and
    // where it is low swell in turn, with the bass.
    float swing = sin(time * 5.0);
    float lobe = clamp(f * 0.5, -1.0, 1.0);
    float lift = max(0.0, lobe * sign(swing)) * abs(swing);

    if (vivid < 0.5) {
        float level = steps(sand);
        float a = level * mix(strength, flash, clamp(0.35 * kick + 0.25 * air, 0.0, 1.0));
        // The plate's swell, far dimmer than the sand, in a few dithered steps.
        float swell = lift * lift * (0.9 * low + 0.5 * kick);
        swell = floor(swell * 3.0 + hash(id + 9.1)) / 3.0;
        a = max(a, swell * strength * 0.5);
        vec3 colour = mix(accent.rgb, shine.rgb, step(0.99, level) * 0.35);
        fragColor = vec4(colour * a, a) * qt_Opacity;
        return;
    }

    // The visual: one plate. Outside it, nothing; a faint rim marks its edge.
    vec2 q = abs(p - 0.5);
    float edge = max(q.x, q.y);
    if (edge > 0.5) {
        fragColor = vec4(0.0);
        return;
    }
    float rim = step(0.5 - 1.2 / span, edge);

    // The swell glows in two colours, one for each half of the wave.
    float glow = lift * lift * (0.10 + 0.55 * low + 0.35 * kick);
    // Dithered into a few steps, like the other pixel visuals.
    glow = floor(glow * 5.0 + hash(id + 9.1)) / 5.0;
    vec3 under = (lobe > 0.0 ? c2.rgb : c3.rgb) * glow * 0.75;

    // Grains thrown up over the plate's busiest places catch the light.
    float glint = step(0.992 - 0.02 * low, hop) * abs(lobe) * (0.3 + 0.7 * gather) * step(0.5, low + kick);

    float level = steps(max(sand, glint));
    vec3 sandColour = mix(accent.rgb, shine.rgb, 0.25 + 0.55 * level + 0.2 * kick);
    vec3 colour = mix(under, sandColour, level);
    colour = max(colour, accent.rgb * rim * 0.28);
    float a = max(max(level, rim * 0.28), max(under.r, max(under.g, under.b)));
    fragColor = vec4(colour, a) * qt_Opacity;
}
