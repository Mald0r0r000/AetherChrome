#include <metal_stdlib>
using namespace metal;

struct ColorMatrixUniforms {
    float3x3 matrix;   // Camera→ProPhoto matrix
};

kernel void colorMatrixKernel(
    texture2d<float, access::read>  inTex   [[texture(0)]],
    texture2d<float, access::write> outTex  [[texture(1)]],
    constant ColorMatrixUniforms&   u       [[buffer(0)]],
    uint2                           gid     [[thread_position_in_grid]])
{
    if (gid.x >= outTex.get_width() || gid.y >= outTex.get_height())
        return;

    float4 pixel = inTex.read(gid);
    float3 rgb   = u.matrix * pixel.rgb;

    // Clamp bas uniquement — préserver HDR
    rgb = max(rgb, float3(0.0F));

    outTex.write(float4(rgb, 1.0F), gid);
}
