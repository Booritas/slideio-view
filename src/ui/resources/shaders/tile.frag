#version 330 core

in vec2 vTexCoord;

uniform sampler2D uTileTexture;
uniform float uAlpha;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(uTileTexture, vTexCoord);
    fragColor = vec4(texel.rgb, texel.a * uAlpha);
}
