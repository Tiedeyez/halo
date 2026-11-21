#version 440

// The Bars visual's finish: the bars (HaloView, drawn as LED cells) shaded
// up their height from the accent to the next palette colour, bloomed like
// lit LEDs (from the source's own mipmaps), and reflected below their base
// in a glossy floor that fades away and ripples a little. The bars fill the
// top `split` of this item; the floor the rest. Premultiplied light.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float split;       // where the bars' base is, 0..1 down this item
    float pulse;       // the beat, 0..1
    float time;
    vec4 c0;           // the base colour (the accent)
    vec4 c1;           // the colour at the top
};

layout(binding = 1) uniform sampler2D source;

vec4 bars(vec2 st) {
    vec4 c = texture(source, st, -8.0);
    // Recolour by height, keeping the cells' stepped shading (their alpha).
    vec3 tint = mix(c0.rgb, c1.rgb, smoothstep(0.85, 0.1, st.y));
    float hot = smoothstep(0.92, 1.0, c.a) * 0.25;   // full cells, and peaks
    return vec4((tint + hot) * c.a, c.a);
}

vec3 bloom(vec2 st) {
    return texture(source, st, 1.5).rgb * 0.5 + texture(source, st, 3.0).rgb * 0.4
         + texture(source, st, 4.5).rgb * 0.35;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec4 col;
    if (uv.y <= split) {
        vec2 st = vec2(uv.x, uv.y / split);
        col = bars(st);
        col.rgb += bloom(st) * mix(c0.rgb, c1.rgb, 0.3) * (1.3 + 1.2 * pulse);
    } else {
        // The floor: the bars mirrored about their base, fading with depth,
        // softened and rippling the further they are from the base.
        float d = (uv.y - split) / max(1.0 - split, 1e-3);         // 0 at the base
        float sy = 1.0 - (uv.y - split) / split;
        float ripple = sin(d * 38.0 - time * 2.4) * 0.0025 * d;
        vec2 st = vec2(uv.x + ripple, sy);
        vec4 r = bars(st);
        r.rgb = mix(r.rgb, bloom(st) * mix(c0.rgb, c1.rgb, 0.3), smoothstep(0.0, 0.7, d));
        float fade = pow(1.0 - d, 1.6) * (0.55 + 0.15 * pulse);
        col = vec4(r.rgb * fade, r.a * fade);
        // A thin line of light where the bars meet the floor.
        col.rgb += exp(-d * 60.0) * c0.rgb * 0.4 * (1.0 + pulse);
    }
    col.rgb = min(col.rgb, vec3(1.0));
    col.a = max(col.a, max(col.r, max(col.g, col.b)));
    fragColor = col * qt_Opacity;
}
