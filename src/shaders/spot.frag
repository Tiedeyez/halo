#version 440

// A small soft light: a lit indicator on a deck (the start/stop button,
// the speed light). A bright core and a wide glow, in `tint`, `strength`
// 0..1. Fills its item; the light is round in the middle of it, or a
// rounded bar when `bar` > 0 (its half length, as a fraction of the half
// width). Premultiplied light, added over what's under it.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 tint;
    float strength;
    float bar;
};

void main() {
    vec2 p = (qt_TexCoord0 - 0.5) * 2.0;           // -1..1 across the item
    p.x = max(abs(p.x) - bar, 0.0);                 // a bar: flat along x
    float r = length(p);
    float core = exp(-r * r * 60.0);
    float glow = exp(-r * r * 6.0) * 0.45;
    vec3 col = tint.rgb * (core * 1.4 + glow) * strength + vec3(core * 0.5 * strength);
    col = min(col, vec3(1.0));
    fragColor = vec4(col, max(col.r, max(col.g, col.b))) * qt_Opacity;
}
