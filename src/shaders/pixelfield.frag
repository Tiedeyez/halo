#version 440

// Halo's window background: a field of square pixels on a grid, mostly dark.
// A few glow and fade like stars; thin columns of pixels rain down, the way
// Omarchy's screensaver effects pour its logo; and on a bass hit a ring of
// pixels sweeps out from the cover. Kept dim so text over it keeps its
// contrast. With time held still (calm), it is a still field.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;      // seconds
    float pulse;     // 0..1, the beat
    float level;     // 0..1, loudness
    float cell;      // pixel size in screen pixels
    float strength;  // brightest a pixel gets, 0..1
    vec2 size;       // item size in screen pixels
    vec2 origin;     // where the beat ring starts, in screen pixels
    vec4 accent;
};

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 px = qt_TexCoord0 * size;
    vec2 id = floor(px / cell);
    vec2 inCell = fract(px / cell);

    // Square pixels with a dark gap between them.
    float gap = 1.0 / cell;
    float inside = step(gap, inCell.x) * step(gap, inCell.y);

    // Stars: one pixel in thirty, each breathing at its own pace.
    float h = hash(id);
    float star = step(0.967, h) * (0.35 + 0.65 * (0.5 + 0.5 * sin(time * (0.4 + h * 1.6) + h * 40.0)));

    // Rain: a few columns carry a falling head with a fading tail.
    float column = hash(vec2(id.x, 7.0));
    float rain = 0.0;
    if (column > 0.93) {
        float rows = size.y / cell;
        float speed = 4.0 + column * 10.0;
        float head = mod(time * speed + column * 300.0, rows + 30.0);
        float behind = head - id.y;
        if (behind >= 0.0 && behind < 14.0)
            rain = (1.0 - behind / 14.0) * (behind < 1.0 ? 1.0 : 0.55);
    }

    // The beat: a ring of pixels sweeping outward, wider on louder music.
    float dist = length((id + 0.5) * cell - origin);
    float reach = (1.0 - pulse) * max(size.x, size.y) * 0.9;
    float ring = pulse * smoothstep(cell * 5.0, 0.0, abs(dist - reach)) * (0.6 + level);

    float b = clamp(max(max(star * 0.7, rain), ring), 0.0, 1.0);
    // Four brightness steps, like block shades.
    b = floor(b * 4.0 + 0.5) / 4.0;
    // Drawn over whatever is behind (premultiplied), so it can sit on the
    // window colour or on Now Playing's cover wash alike.
    float a = b * strength * inside;
    fragColor = vec4(accent.rgb * a, a) * qt_Opacity;
}
