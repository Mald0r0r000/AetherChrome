#include <metal_stdlib>
using namespace metal;

struct OutputUniforms {
    float3x3 proPhotoToDisplayMatrix;  // ProPhoto→sRGB or ProPhoto→P3
    float    gamma;                    // 2.2 for sRGB approx, 1.0 HDR
};

kernel void outputTransformKernel(
    texture2d<float, access::read>  inTex  [[texture(0)]],
    texture2d<float, access::write> outTex [[texture(1)]],
    constant OutputUniforms&        u      [[buffer(0)]],
    uint2                           gid    [[thread_position_in_grid]])
{
    if (gid.x >= outTex.get_width() || gid.y >= outTex.get_height())
        return;

    float4 pixel = inTex.read(gid);
    float3 rgb   = u.proPhotoToDisplayMatrix * pixel.rgb;

    // Clamp [0,1] — display space
    rgb = clamp(rgb, float3(0.0F), float3(1.0F));

    // OETF : approximation gamma (e.g., sRGB ~= 2.2, linear = 1.0)
    if (u.gamma > 1.0F)
        rgb = pow(rgb, float3(1.0F / u.gamma));

    outTex.write(float4(rgb, 1.0F), gid);
}
