#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec4 uScreenRect; // x, y, width, height in screen pixels
uniform vec2 uViewportSize; // viewport width, height in pixels

out vec2 vTexCoord;

void main()
{
    // aPos is a unit quad: (0,0) to (1,1)
    // Map to the screen rect position
    float screenX = uScreenRect.x + aPos.x * uScreenRect.z;
    float screenY = uScreenRect.y + aPos.y * uScreenRect.w;

    // Convert screen pixels to NDC: x in [-1,1], y in [-1,1]
    // Screen origin is top-left, NDC origin is bottom-left center
    float ndcX = (screenX / uViewportSize.x) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / uViewportSize.y) * 2.0;

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);

    // Texture coordinates: flip Y so top of texture maps to top of quad
    vTexCoord = vec2(aPos.x, aPos.y);
}
