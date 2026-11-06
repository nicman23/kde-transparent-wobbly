#version 140

uniform sampler2D windowTexture;
uniform sampler2D background;
uniform float opacity;
uniform mat4 colorMatrix; // saturation, as GPU Blur
uniform float noise; // strength, 0 is off
uniform float noiseScale; // grain size, device px

in vec2 texcoord;
in vec2 backgroundTexcoord;
in vec2 frameCoord;

out vec4 fragColor;

// static per grain, the same noise as GPU Blur's
float hash(vec2 p)
{
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

void main()
{
    vec4 window = texture(windowTexture, texcoord) * opacity;
    // frost only behind the frame, the shadow around it stays a plain shadow
    vec2 inside = step(vec2(0.0), frameCoord) * step(frameCoord, vec2(1.0));
    float frost = inside.x * inside.y;
    vec3 behind = (texture(background, backgroundTexcoord) * colorMatrix).rgb
        + floor(hash(floor(gl_FragCoord.xy / noiseScale)) * noise * 255.0) / 255.0;
    fragColor = vec4(window.rgb + behind * (1.0 - window.a) * frost, window.a + (1.0 - window.a) * frost);
}
