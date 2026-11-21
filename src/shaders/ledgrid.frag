#version 440

// The pixel visuals as a wall of LEDs in a dark room: a small texture, one
// texel per cell, enlarged into square screen pixels with a dark gap between
// them, and each cell lit like an LED:
//   - a little brighter in its middle than at its edges;
//   - the hottest cells burn white at the core;
//   - light blooms onto the neighbours and into the gaps, taken from the
//     texture's own mipmaps (2, 4 and 8 cells wide);
//   - the beat lifts the brightness and the bloom.
// Premultiplied, like the texture.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 cells;        // the texture's size, in cells
    float cell;        // one cell, in screen pixels
    vec4 gapColour;    // what shows between cells (premultiplied)
    float pulse;       // the beat, 0..1
    float glow;        // the bloom's strength (0 turns it off)
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec2 at = qt_TexCoord0 * cells;
    vec2 id = floor(at);
    vec2 f = fract(at);
    float gap = 1.0 / cell;
    float inside = step(gap, f.x) * step(gap, f.y);
    vec2 centre = (id + 0.5) / cells;
    // A texel's centre, so the cell's exact value even with smooth filtering.
    vec4 c = texture(source, centre, -8.0);

    // The LED: brighter in the middle; the hottest burn white at the core.
    vec2 o = (f - 0.5 - gap * 0.5) * 2.0;           // -1..1 across the lit part
    float r2 = dot(o, o);
    float lum = max(c.r, max(c.g, c.b));
    float profile = 1.08 - 0.22 * r2;
    float hot = smoothstep(0.6, 1.0, lum) * exp(-r2 * 2.5) * 0.45;
    vec4 led = vec4(c.rgb * profile + vec3(hot) * c.a, c.a);
    led *= 1.0 + 0.22 * pulse;

    // Bloom from the mipmaps. Enlarged, the texture is sampled at about
    // level -log2(cell); the bias lifts that to levels 1, 2 and 3.
    float base = log2(max(cell, 1.0));
    vec3 bloom = texture(source, qt_TexCoord0, base + 1.0).rgb * 0.45
               + texture(source, qt_TexCoord0, base + 2.0).rgb * 0.35
               + texture(source, qt_TexCoord0, base + 3.0).rgb * 0.3;
    bloom *= glow * (0.8 + 0.7 * pulse);

    vec4 col = mix(gapColour * c.a, led, inside);
    col.rgb += bloom * (1.0 - 0.5 * inside * c.a);
    col.rgb = min(col.rgb, vec3(1.0));
    col.a = max(col.a, max(col.r, max(col.g, col.b)));
    fragColor = col * qt_Opacity;
}
