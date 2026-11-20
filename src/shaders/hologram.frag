#version 440

// The Hologram visual: Halo's turntable made of light. Blender drew the deck
// as two passes, both white on black (art/turntable/turntable.py --energy):
// `lines`, every edge the camera sees plus faint hidden ones, and `body`, a
// fresnel rim where surfaces turn away. Here they are tinted with the theme,
// bloomed (the lines' own mipmaps), and given energy:
//   - the record's grooves carry the spectrum, bass at the rim, highs by the
//     label, with a sweep that turns at 33 1/3 and a shockwave on each beat;
//   - the groove the stylus is reading burns brighter;
//   - the label is the album cover as light, turning with the record;
//   - scanlines, a slow scan band and a flicker of hologram instability.
// Output is light: premultiplied, as opaque as it is bright, so over the
// dark window it reads as added light. The tonearm and spindle use the same shader with
// `deck` = 0 (no record, no label).
//
// `h0..h2` are the rows of the homography from this item's uv (0..1, y down)
// to the label's plane in label radii, from Blender's camera.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float deck;        // 1 for the deck layer, 0 for arm and spindle
    float gain;        // the layer's strength (crossfades of the arm frames)
    float spin;        // the record's angle, radians
    float time;        // seconds, for the scan band and flicker
    float pulse;       // the beat, 0..1, peaks on the kick
    float level;       // overall loudness, 0..1
    float high;        // treble, 0..1
    float progress;    // through the song, 0..1
    float hasCover;
    float recordR;     // the record's radius, in label radii
    float grooveOut;   // where the music starts and ends, in label radii
    float grooveIn;
    float lineRows;    // scanlines across the item's height
    vec4 bandsLo;      // the spectrum, 8 bands, bass first
    vec4 bandsHi;
    vec4 h0;
    vec4 h1;
    vec4 h2;
    vec4 c0;           // palette: lines
    vec4 c1;           // bloom and energy
    vec4 c2;           // body
    vec4 c3;           // accents (stylus groove, shockwave)
};

layout(binding = 1) uniform sampler2D lines;
layout(binding = 2) uniform sampler2D body;
layout(binding = 3) uniform sampler2D cover;

float band(float x) {
    // x 0..1 across the 8 bands, linearly between them (weights, not
    // indexing, so it compiles for GLSL ES 1.00 too).
    float f = clamp(x, 0.0, 1.0) * 7.0;
    return bandsLo.x * max(0.0, 1.0 - abs(f - 0.0)) + bandsLo.y * max(0.0, 1.0 - abs(f - 1.0))
         + bandsLo.z * max(0.0, 1.0 - abs(f - 2.0)) + bandsLo.w * max(0.0, 1.0 - abs(f - 3.0))
         + bandsHi.x * max(0.0, 1.0 - abs(f - 4.0)) + bandsHi.y * max(0.0, 1.0 - abs(f - 5.0))
         + bandsHi.z * max(0.0, 1.0 - abs(f - 6.0)) + bandsHi.w * max(0.0, 1.0 - abs(f - 7.0));
}

float hash(float n) { return fract(sin(n) * 43758.5453); }

void main() {
    vec2 uv = qt_TexCoord0;
    float kick = max(0.0, pulse * 1.6 - 0.6);

    // ---- the drawing, as light ----
    // A hair of chromatic split on the kick: the lines' red and blue sides.
    float split = 0.0012 * kick;
    float l = texture(lines, uv).r;
    float lr = texture(lines, uv + vec2(split, 0.0)).r;
    float lb = texture(lines, uv - vec2(split, 0.0)).r;
    // Bloom from the lines' own mipmaps (a bias, which every GLSL has).
    float bloom = texture(lines, uv, 2.5).r * 0.9 + texture(lines, uv, 4.5).r * 0.8 + texture(lines, uv, 6.0).r * 0.6;
    float skin = texture(body, uv).r;

    vec3 ink = mix(c0.rgb, c1.rgb, smoothstep(0.15, 0.95, uv.y));
    vec3 col = vec3(lr, l, lb) * ink * (1.05 + 0.6 * kick + 0.25 * level);
    col += bloom * c1.rgb * (0.35 + 0.45 * level + 0.5 * kick);
    col += skin * c2.rgb * (0.28 + 0.3 * level);

    // ---- the record: grooves of energy, the label as light ----
    if (deck > 0.5) {
        vec3 p = vec3(dot(h0.xyz, vec3(uv, 1.0)), dot(h1.xyz, vec3(uv, 1.0)), dot(h2.xyz, vec3(uv, 1.0)));
        vec2 q = p.xy / p.z;
        float r = length(q);
        float aa = fwidth(r);
        float a = atan(q.y, q.x);

        if (r > 1.02 && r < recordR) {
            // How far across the playing surface: 0 at the rim, 1 at the label.
            float across = clamp((recordR - r) / (recordR - 1.0), 0.0, 1.0);
            float energy = band(across);
            // Fine grooves, thinned so they read as lines at any size.
            float g = abs(fract(r * 14.0) - 0.5);
            float groove = 1.0 - smoothstep(0.0, max(aa * 14.0, 0.07), g);
            // A sweep of light turning with the record, two arms.
            float sweep = pow(0.5 + 0.5 * cos(2.0 * (a + spin)), 10.0);
            float lit = groove * (0.10 + energy * 1.35) + sweep * (0.10 + 0.5 * energy) * (0.4 + groove);
            col += lit * mix(c1.rgb, c0.rgb, across);

            // The beat: a ring of light running out from the label to the rim.
            float front = 1.0 + (1.0 - pulse) * (recordR - 1.0);
            col += exp(-abs(r - front) * 22.0) * pulse * 0.9 * c3.rgb;

            // The groove under the stylus.
            float playing = grooveOut + (grooveIn - grooveOut) * clamp(progress, 0.0, 1.0);
            col += exp(-abs(r - playing) * 70.0) * (0.55 + 0.6 * level) * c3.rgb;
        }

        if (r < 1.0 + aa) {
            float inside = 1.0 - smoothstep(1.0 - aa * 1.5, 1.0, r);
            if (hasCover > 0.5) {
                float c = cos(spin), s = sin(spin);
                vec2 t = vec2(c * q.x - s * q.y, s * q.x + c * q.y);
                vec3 art = texture(cover, vec2(0.5 + 0.5 * t.x, 0.5 - 0.5 * t.y)).rgb;
                // Light, not paint: the cover glows, tinted a little toward
                // the palette, and breathes with the music.
                vec3 glow = mix(art, art * (0.6 + c1.rgb * 0.6), 0.25);
                col += glow * inside * (0.75 + 0.35 * level + 0.25 * kick);
            } else {
                col += inside * c2.rgb * 0.25;
            }
            // The label's edge, a bright rim.
            col += exp(-abs(r - 1.0) * 60.0) * c0.rgb * 0.8;
        }
    }

    // ---- hologram ----
    float row = gl_FragCoord.y;
    float scan = 0.82 + 0.18 * sin(uv.y * lineRows * 3.14159);
    float band1 = fract(time * 0.12);
    float bandGlow = exp(-abs(uv.y - (1.0 - band1)) * 28.0) * 0.35;
    // Now and then a row slips: hologram instability, more on hard highs.
    float flick = 1.0 - 0.06 * step(0.985 - high * 0.03, hash(floor(time * 24.0) + floor(row / 3.0) * 0.37));
    col *= scan * flick;
    col += (l + skin * 0.5 + bloom * 0.3) * bandGlow * c1.rgb;

    // Premultiplied light: as opaque as it is bright, so it lays over the
    // window like added light and still survives being drawn into a layer.
    col = min(col * gain, vec3(1.0));
    float a = max(col.r, max(col.g, col.b));
    fragColor = vec4(col, a) * qt_Opacity;
}
