#version 140

// One direction of a separable Gaussian: 9 taps folded into 5 bilinear fetches.
uniform sampler2D source;
uniform vec2 direction; // one tap step, in source texture coordinates

in vec2 uv;

out vec4 fragColor;

void main()
{
    vec2 near = direction * 1.3846153846;
    vec2 far = direction * 3.2307692308;
    fragColor = texture(source, uv) * 0.2270270270
        + (texture(source, uv + near) + texture(source, uv - near)) * 0.3162162162
        + (texture(source, uv + far) + texture(source, uv - far)) * 0.0702702703;
}
