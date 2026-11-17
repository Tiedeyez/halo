#version 440

// A progress bar of square pixel cells: played cells in the accent, the
// stretch where the next song blends in dithered, the rest dim.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float fraction;   // played, 0..1
    float blendFrom;  // where the blend starts, 0..1, or -1
    float cell;       // cell width in screen pixels
    float gap;        // gap between cells in screen pixels
    vec2 size;
    vec4 played;
    vec4 rest;
    vec4 blend;
};

void main() {
    float x = qt_TexCoord0.x * size.x;
    float pitch = cell + gap;
    float index = floor(x / pitch);
    if (mod(x, pitch) >= cell) {
        fragColor = vec4(0.0);
        return;
    }
    float at = (index * pitch + cell * 0.5) / size.x;
    vec4 c = rest;
    if (blendFrom >= 0.0 && at >= blendFrom)
        c = mod(index, 2.0) < 1.0 ? blend : rest; // dithered, like a shaded block
    if (at <= fraction)
        c = played;
    fragColor = c * qt_Opacity;
}
