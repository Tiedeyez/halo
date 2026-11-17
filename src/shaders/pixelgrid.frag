#version 440

// Enlarges a small texture into square screen pixels with a dark gap between
// them, sampling each cell at its centre. Used by the Mandelbrot visual,
// which computes one value per cell into the texture.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 cells;     // the texture's size, in cells
    float cell;     // one cell, in screen pixels
    vec4 gapColour; // what shows between cells (premultiplied)
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec2 at = qt_TexCoord0 * cells;
    vec2 id = floor(at);
    vec2 inCell = fract(at);
    float gap = 1.0 / cell;
    float inside = step(gap, inCell.x) * step(gap, inCell.y);
    vec4 c = texture(source, (id + 0.5) / cells);
    fragColor = mix(gapColour * c.a, c, inside) * qt_Opacity;
}
