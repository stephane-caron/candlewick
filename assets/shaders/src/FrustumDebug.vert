#version 450

layout(set=1, binding=0) uniform DebugTransform {
   mat4 invProj;
   mat4 mvp;
   vec4 color;
   vec3 eyePos;
};

layout(location=0) out vec4 outColor;

// Line indices as array (first vertex, second vertex)
const uint[24] LINE_INDICES = uint[](
    // Bottom face
    0, 1,  // x-edge
    0, 2,  // y-edge
    2, 3,  // x-edge
    1, 3,  // y-edge
    // Top face
    4, 5,  // x-edge
    4, 6,  // y-edge
    6, 7,  // x-edge
    5, 7,  // y-edge
    // Vertical edges
    0, 4,  // z-edge
    1, 5,  // z-edge
    2, 6,  // z-edge
    3, 7   // z-edge
);

void main() {
    if (gl_VertexIndex < 24) {
        uint cornerIdx = LINE_INDICES[gl_VertexIndex];
        uint x = cornerIdx & 1;
        uint y = (cornerIdx >> 1) & 1;
        uint z = (cornerIdx >> 2) & 1;

        vec4 ndc = vec4(
            2.0 * float(x) - 1.0,
            2.0 * float(y) - 1.0,
            2.0 * float(z) - 1.0,
            1.0
        );

        vec4 viewPos = invProj * ndc;
        viewPos /= viewPos.w;
        gl_Position = mvp * viewPos;
    } else if (gl_VertexIndex >= 24) {
        uint axisIdx = (gl_VertexIndex - 24) / 2;  // 0,1,2 for x,y,z
        uint pointIdx = gl_VertexIndex & 1;        // 0,1 for start/end

        float size = 0.1;  // Size of marker
        vec3 offset = vec3(0.0);
        offset[axisIdx] = pointIdx == 0 ? -size : size;

        gl_Position = mvp * vec4(eyePos + offset, 1.0);
    }
    outColor = color;
}
