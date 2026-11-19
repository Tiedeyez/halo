#version 440

// The Hologram visual: a record made of light. A pixel disc tilted toward
// you, spinning at 33 1/3, its grooves a thin-film rainbow in the theme's
// colours that shifts with the angle you see it from, two fixed bands of
// glare sweeping over it as it turns, and the album cover as its label. The
// spectrum lights the grooves where each frequency sits (bass outside,
// treble near the label); a beat throws a bright ring across it. A faint
// projector beam rises from below, scanlines and a slow flicker give it away
// as a hologram, and the tonearm creeps inward across the song.
//
// Drawn into a small texture, one fragment per cell; pixelgrid.frag enlarges it.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;      // the grid, in cells
    float spin;     // the record's angle, radians
    float time;     // seconds, for flicker and shimmer
    float pulse;    // the beat, 0..1
    float kick;     // 1 on a beat, falling fast
    float level;    // loudness 0..1
    float high;     // treble 0..1
    float progress; // 0..1 through the song (the tonearm)
    float hasCover; // 1 when a cover is shown as the label
    vec4 bandsLo;   // eight band groups, bass first
    vec4 bandsHi;
    vec4 c0;        // accent
    vec4 c1;        // cyan
    vec4 c2;        // blue
    vec4 c3;        // magenta
    vec4 c5;        // foreground
    vec4 window;
};

layout(binding = 1) uniform sampler2D cover;

const float Tau = 6.28318530718;
const float Tilt = 0.74;          // the disc seen from a little above
const float Disc = 0.44;          // radius, in units of the stage's side
const float Label = 0.155;
const float Hole = 0.012;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// The theme's colours in a loop, for the thin-film rainbow.
vec3 film(float t) {
    t = fract(t) * 4.0;
    float f = smoothstep(0.0, 1.0, fract(t));
    int i = int(floor(t));
    vec3 a = i == 0 ? c1.rgb : i == 1 ? c2.rgb : i == 2 ? c3.rgb : c0.rgb;
    vec3 b = i == 0 ? c2.rgb : i == 1 ? c3.rgb : i == 2 ? c0.rgb : c1.rgb;
    return mix(a, b, f);
}

float bandAt(float u) {
    // u 0..1 from the outer edge (bass) to the label (treble).
    float x = clamp(u, 0.0, 0.999) * 7.0;
    float f = fract(x);
    float i = floor(x);
    float v0 = i < 0.5 ? bandsLo.x : i < 1.5 ? bandsLo.y : i < 2.5 ? bandsLo.z : i < 3.5 ? bandsLo.w
             : i < 4.5 ? bandsHi.x : i < 5.5 ? bandsHi.y : bandsHi.z;
    float v1 = i < 0.5 ? bandsLo.y : i < 1.5 ? bandsLo.z : i < 2.5 ? bandsLo.w : i < 3.5 ? bandsHi.x
             : i < 4.5 ? bandsHi.y : i < 5.5 ? bandsHi.z : bandsHi.w;
    return mix(v0, v1, f);
}

// A 4 x 4 Bayer threshold (0..1), so shading falls into crisp pixel steps.
// Arithmetic only, so it compiles for every GLSL target qsb writes.
float bayer2(vec2 a) {
    a = floor(a);
    return fract(dot(a, vec2(0.5, a.y * 0.75)));
}
float bayer(vec2 id) {
    return bayer2(0.5 * id) * 0.25 + bayer2(id);
}

vec3 quantise(vec3 c, vec2 id) {
    const float Steps = 7.0;
    return floor(c * Steps + bayer(id)) / Steps;
}

void main() {
    vec2 id = floor(qt_TexCoord0 * size);
    float side = min(size.x, size.y);
    vec2 uv = (id + 0.5 - size * 0.5) / side;      // -0.5..0.5, y down
    uv.y -= 0.03;                                    // a touch above centre, room for the beam

    // The disc in its own plane: undo the tilt.
    vec2 d = vec2(uv.x, uv.y / Tilt);
    float r = length(d);
    vec3 colour = vec3(0.0);
    float alpha = 0.0;

    // The projector beam: a soft cone of light from below the disc.
    float beamX = abs(uv.x) / max(0.02, (0.46 - uv.y) * 0.55);
    float beam = (1.0 - smoothstep(0.0, 1.0, beamX)) * smoothstep(-0.05, 0.42, uv.y) * 0.16 * (0.7 + 0.3 * level);
    colour += c1.rgb * beam;
    alpha += beam;

    if (r < Disc) {
        float angle = atan(d.y, d.x);
        float turned = angle - spin;
        float u = (Disc - r) / (Disc - Label);       // 0 at the edge, 1 at the label

        if (r < Label) {
            if (r < Hole) {
                colour = window.rgb;
                alpha = 1.0;
            } else {
                // The label: the cover, turning with the record, cut round.
                vec2 p = vec2(cos(turned), sin(turned)) * (r / Label) * 0.5;
                vec3 art = hasCover > 0.5 ? texture(cover, p * 0.98 + 0.5).rgb : mix(c0.rgb, c3.rgb, 0.5 + 0.5 * sin(turned * 2.0));
                // A hologram is light: the label glows a little and scans.
                art = mix(art, art * 1.25 + c5.rgb * 0.05, 0.4);
                float rim = smoothstep(Label - 0.012, Label, r);
                colour = mix(art, c0.rgb, rim * 0.8);
                alpha = 0.95;
            }
        } else {
            // Grooves: fine rings, modulated a little by the music so they
            // seem to carry it.
            float groove = 0.5 + 0.5 * sin(r * 520.0 + bandAt(u) * 3.0);
            // Thin-film colour: depends on where the light is, not where
            // the record has turned to, so it stays put as the disc spins.
            float view = angle * 0.95 + r * 9.0 + time * 0.04;
            vec3 base = film(view / Tau * 2.0);
            // Two bands of glare across the disc, fixed in the room.
            float glare = pow(abs(cos(angle - 0.8)), 26.0) * 0.9 + pow(abs(cos(angle + 2.1)), 40.0) * 0.5;
            // Where the spectrum sits: bass outside, treble inside.
            float energy = bandAt(u);
            float lit = 0.22 + 0.55 * energy * energy + 0.25 * groove;
            colour = mix(window.rgb * 1.6, base, lit) + glare * mix(c5.rgb, base, 0.3) * (0.5 + 0.5 * groove);
            // A beat throws a ring of light out across the grooves.
            float ring = (1.0 - kick) * (Disc - Label) + Label;
            colour += c0.rgb * kick * smoothstep(0.03, 0.0, abs(r - ring)) * 1.2;
            // The edge catches the light.
            colour = mix(colour, c5.rgb, smoothstep(Disc - 0.010, Disc, r) * 0.7);
            // Glitter: a few cells sparkle with the treble.
            float g = hash(id + floor(time * 9.0));
            colour += c5.rgb * step(0.985 - 0.02 * high, g) * 0.8;
            alpha = 0.92;
        }
        // Hologram tells: every other row dimmer, a slow flicker, and a
        // bright scan line drifting down.
        float scan = mod(id.y, 2.0) < 1.0 ? 1.0 : 0.78;
        float flicker = 0.94 + 0.06 * sin(time * 23.0) * sin(time * 3.7);
        float sweep = smoothstep(0.035, 0.0, abs(fract(time * 0.18) - (uv.y + 0.5))) * 0.35;
        colour = colour * scan * flicker + c1.rgb * sweep;
        // The shadow of the disc's thickness along its near edge.
        float lip = smoothstep(Disc, Disc - 0.02, r) * step(0.0, d.y);
        colour *= mix(1.0, 0.85, (1.0 - lip) * step(Disc - 0.02, r));
    }

    // The tonearm: from its pivot at the top right to the needle, which
    // moves from the edge toward the label as the song plays.
    vec2 pivot = vec2(0.40, -0.40);
    float needleR = mix(Disc - 0.03, Label + 0.04, clamp(progress, 0.0, 1.0));
    vec2 needleD = vec2(cos(-0.55), sin(-0.55)) * needleR;
    vec2 needle = vec2(needleD.x, needleD.y * Tilt);
    vec2 pa = uv - pivot, ba = needle - pivot;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    float arm = length(pa - ba * h);
    if (arm < 0.009) {
        colour = mix(c5.rgb, c0.rgb, h * h);
        alpha = 1.0;
    }
    if (length(uv - pivot) < 0.03) {
        colour = mix(window.rgb * 2.0, c5.rgb, 0.35);
        alpha = 1.0;
    }
    if (length(uv - needle) < 0.014) {
        colour = c0.rgb * (1.0 + 0.5 * pulse);
        alpha = 1.0;
    }

    colour = quantise(clamp(colour, 0.0, 1.0), id);
    alpha = clamp(alpha, 0.0, 1.0);
    fragColor = vec4(colour * alpha, alpha) * qt_Opacity;
}
