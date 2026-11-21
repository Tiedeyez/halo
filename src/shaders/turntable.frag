#version 440

// The Turntable visual's deck: the rendered deck, its lamps' light tinted
// with the theme's accent and lifted by the beat, and the album cover printed
// on the record's label in perspective, turning at 33 1/3.
//
// The label's place comes from Blender: `h0..h2` are the rows of the
// homography from the image (0..1, y down) to the label's plane, in label
// radii (+x right, +y away from the viewer). The deck was rendered with a
// white label, so its brightness there is the light falling on the label;
// the cover is multiplied by it, and keeps the render's shading and sheen.
//
// On the vinyl: specks of dust that ride round with the record and show
// where the sheen catches them (so it is seen to turn), the smooth gaps
// between the album's songs, and on the kick a flash of the stage light, in
// the accent, in the record's sheen.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float spin;      // the record's angle, radians
    float glowLevel; // the lamps' strength, ~1 at rest
    float hasCover;  // 1 when the cover is ready
    vec4 h0;
    vec4 h1;
    vec4 h2;
    vec4 tint;       // the lamps' colour
    float pulse;     // the beat, 0..1
    float level;     // loudness, 0..1
    float recordR;   // the record's radius, in label radii
    float grooveOut; // where the music starts and ends, in label radii
    float grooveIn;
    float tracks;    // songs on the side, for the gaps between them
};

vec4 hash4(vec2 p) {
    vec4 q = fract(vec4(p.xyxy) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    q += dot(q, q.wzxy + 33.33);
    return fract((q.xxyz + q.yzzw) * q.zywx);
}
float hash1(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}

layout(binding = 1) uniform sampler2D deck;
layout(binding = 2) uniform sampler2D glow;
layout(binding = 3) uniform sampler2D cover;

void main() {
    vec2 uv = qt_TexCoord0;
    vec4 base = texture(deck, uv);          // premultiplied
    vec3 colour = base.rgb;

    vec3 p = vec3(dot(h0.xyz, vec3(uv, 1.0)), dot(h1.xyz, vec3(uv, 1.0)), dot(h2.xyz, vec3(uv, 1.0)));
    vec2 q = p.xy / p.z;
    float r = length(q);
    float c = cos(spin), s = sin(spin);
    vec2 t = vec2(c * q.x - s * q.y, s * q.x + c * q.y);   // q on the turning record

    if (r > 1.03 && r < recordR - 0.01 && base.a > 0.5) {
        vec3 unlit = colour / max(base.a, 1e-4);
        float sheen = dot(unlit, vec3(0.2126, 0.7152, 0.0722));
        float aa = max(fwidth(r), 1e-4);

        // The gaps between songs: no groove, so mirror-smooth; they turn the
        // sheen away and read as darker rings, with a fine bright lip.
        float gaps = 0.0;
        for (int k = 1; k < 12; ++k) {
            if (float(k) >= tracks)
                break;
            // Song lengths vary: the gaps are spread with a little jitter.
            float f = (float(k) + (hash1(float(k) * 7.0 + tracks) - 0.5) * 0.5) / tracks;
            float rk = mix(grooveOut, grooveIn, f);
            float x = (r - rk) / max(0.006, aa * 1.2);
            gaps += exp(-x * x);
        }
        // Lead-in and run-out are smooth too.
        gaps += smoothstep(grooveOut + 0.01, grooveOut + 0.04, r) * 0.5 + smoothstep(grooveIn - 0.01, grooveIn - 0.06, r) * 0.6;
        colour *= 1.0 - 0.32 * clamp(gaps, 0.0, 1.0) * smoothstep(0.02, 0.25, sheen);

        // Dust: about one speck in forty cells of the turning record, lit
        // by the sheen (and a little by the room).
        vec2 g = t * 16.0;
        vec2 cell = floor(g);
        vec4 h = hash4(cell + 17.0);
        if (h.w > 0.975) {
            vec2 at = 0.25 + 0.5 * h.xy;
            float size = max(0.04 + 0.08 * h.z, aa * 16.0 * 0.7);
            float dd = length(fract(g) - at) / size;
            float speck = exp(-dd * dd * 2.0);
            colour += speck * (0.015 + 1.1 * sheen) * base.a;
        }

        // The kick: the stage light flashes in the sheen, in the accent.
        colour += tint.rgb * pulse * pulse * smoothstep(0.03, 0.3, sheen) * 0.9 * base.a;
    }

    if (hasCover > 0.5 && r < 1.02) {
        // Clockwise seen from above: sample the cover turned the other way.
        vec3 art = texture(cover, vec2(0.5 + 0.5 * t.x, 0.5 - 0.5 * t.y)).rgb;
        // The white label's brightness, 0.8 albedo under the render's light.
        float light = dot(colour / max(base.a, 1e-4), vec3(0.2126, 0.7152, 0.0722));
        vec3 lit = art * clamp(light * 1.2, 0.0, 1.25) * base.a;
        float edge = fwidth(r) * 1.5;
        colour = mix(colour, lit, 1.0 - smoothstep(1.0 - edge, 1.0, r));
    }

    // The lamps: their light alone, tinted, added (inside the deck's alpha).
    float g = texture(glow, uv).r;
    colour += g * tint.rgb * glowLevel * base.a;

    fragColor = vec4(colour, base.a) * qt_Opacity;
}
