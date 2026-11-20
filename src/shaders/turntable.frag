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
};

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
    if (hasCover > 0.5 && r < 1.02) {
        // Clockwise seen from above: sample the cover turned the other way.
        float c = cos(spin), s = sin(spin);
        vec2 t = vec2(c * q.x - s * q.y, s * q.x + c * q.y);
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
