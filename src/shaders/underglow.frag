#version 440

// The Turntable's underglow: light of the theme's accent spilling from under
// the plinth onto the dark around it, as from a strip of LEDs beneath the
// deck. A soft falloff from the plinth's rounded outline, a little stronger
// below it (the light pools toward the viewer's side), slowly drifting
// between the accent and a second palette colour. The deck is drawn over it,
// so only the spill outside the plinth shows. Premultiplied light.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 plinth;       // x, y, w, h of the plinth, 0..1 of the (square) item
    float strength;    // ~0.3 at rest; bass and the kick lift it
    float time;
    vec4 tint;
    vec4 tint2;
};

// Signed distance to a rounded box (centre c, half size b, corner r).
float roundedBox(vec2 p, vec2 c, vec2 b, float r) {
    vec2 q = abs(p - c) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 c = plinth.xy + plinth.zw * 0.5;
    float d = roundedBox(uv, c, plinth.zw * 0.5, 0.012);
    float outside = max(d, 0.0);
    // Two falloffs: a tight bright edge and a wide soft pool.
    float glow = exp(-outside / 0.012) * 0.55 + exp(-outside / 0.07) * 0.45;
    // Pooling toward the front (down the image) and a little at the sides.
    float pool = 0.75 + 0.35 * smoothstep(c.y - plinth.w * 0.5, c.y + plinth.w * 0.7, uv.y);
    // The colour drifts slowly round the plinth between the two.
    float a = atan(uv.y - c.y, uv.x - c.x);
    vec3 col = mix(tint.rgb, tint2.rgb, 0.5 + 0.5 * sin(a * 2.0 + time * 0.4));
    col *= glow * pool * strength;
    // Under the plinth itself, nothing (the deck covers it anyway).
    col *= smoothstep(-0.004, 0.002, d);
    col = min(col, vec3(1.0));
    float alpha = max(col.r, max(col.g, col.b));
    fragColor = vec4(col, alpha) * qt_Opacity;
}
