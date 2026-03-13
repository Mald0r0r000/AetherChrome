#include <metal_stdlib>
using namespace metal;

struct ToneMapUniforms {
    int   op;           // 0=Linear, 1=FilmicS, 2=ACES
    float contrast;
    float saturation;
    float whiteClip;
};

inline float3 filmicS(float3 x) {
    const float A = 0.15F, B = 0.50F, C = 0.10F, D = 0.20F, E = 0.02F, F = 0.30F;
    // We cannot use lambdas easily in Metal for this structure, so manual calculation:
    float3 num = (x * (A * x + C * B) + D * E);
    float3 den = (x * (A * x + B) + D * F);
    float3 v = (num / den) - (E / F);
    
    // Uncharted 2 Filmic scale factor (f(11.2) approx for this curve = 0.8466f)
    const float scale = 0.8466F;
    return v / scale;
}

inline float3 acesApprox(float3 x) {
    return clamp((x * (2.51F * x + 0.03F)) / (x * (2.43F * x + 0.59F) + 0.14F),
                 0.0F, 1.0F);
}

kernel void toneMappingKernel(
    texture2d<float, access::read>  inTex  [[texture(0)]],
    texture2d<float, access::write> outTex [[texture(1)]],
    constant ToneMapUniforms&       u      [[buffer(0)]],
    uint2                           gid    [[thread_position_in_grid]])
{
    if (gid.x >= outTex.get_width() || gid.y >= outTex.get_height())
        return;

    float4 p   = inTex.read(gid);
    float3 rgb = p.rgb * u.contrast;

    if      (u.op == 1) rgb = filmicS(rgb * 2.0F);
    else if (u.op == 2) rgb = acesApprox(rgb);
    else                rgb = clamp(rgb, 0.0F, 1.0F);

    // Saturation using Rec.709 luma weights
    float luma = dot(rgb, float3(0.2126F, 0.7152F, 0.0722F));
    rgb = mix(float3(luma), rgb, u.saturation);

    outTex.write(float4(clamp(rgb, 0.0F, 1.0F), 1.0F), gid);
}
