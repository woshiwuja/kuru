// Ported from outline.gsls, a Shadertoy "Buffer B" edge pass that read a
// G-buffer of (rgb = normal, a = ray distance) and darkened its discontinuities.
// Notes on the port:
//  - vec2/vec3/vec4 -> float2/float3/float4, texture() -> SampleLevel()
//  - The original's camera block (calcLookAtMatrix / getRay / squareFrame, ro,
//    rd, pos) only existed to shade the raymarched scene it was compositing
//    over. The game rasterises and shades that itself, so only getDeltas and
//    the final `col -= deltas.x - deltas.y` survive.
//  - That subtract is the blend, not an arithmetic op here: the outline pass
//    draws over the already-shaded frame with eReverseSubtract (dst - src),
//    so the fragment returns the darkening amount.
//  - The G-buffer the original assumed doesn't exist in this renderer, so this
//    file also carries the prepass that fills one - prepassVert/prepassFrag
//    draw the scene's normals and view distance into it before the main pass.

// ---- normal + distance prepass ---------------------------------------------

// Same layout as slang.slang's UBO (and vertex.hpp's UniformBufferObject): the
// prepass reuses RenderPlugin's mesh pipeline layout and its per-entity
// descriptor sets, so it must see binding 0 the same way. The texture at
// binding 1 is simply not read here.
static const uint MAX_LIGHTS = 4;

struct GPULight {
    float4 direction;
    float4 color;
};

struct UniformBufferObject {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float4 material;
    GPULight lights[MAX_LIGHTS];
    uint lightCount;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;

struct PrepassOutput {
    float4 position : SV_Position;
    float3 worldPos;
    float3 worldNormal;
};

// Parameters map to vertex input locations 0, 1, 2, 3 in declaration order, so
// all four are declared even though colour and uv are unused here - dropping
// them would shift `normal` off location 3.
[shader("vertex")]
PrepassOutput prepassVert(float3 position, float3 color, float2 uv, float3 normal) {
    PrepassOutput output;
    float4 world = mul(ubo.model, float4(position, 1.0));
    output.position = mul(ubo.proj, mul(ubo.view, world));
    output.worldPos = world.xyz;
    float3x3 normalMatrix = float3x3(ubo.model[0].xyz, ubo.model[1].xyz, ubo.model[2].xyz);
    output.worldNormal = normalize(mul(normalMatrix, normal));
    return output;
}

[shader("fragment")]
float4 prepassFrag(float3 worldPos, float3 worldNormal) : SV_Target {
    // Same normal choice as slang.slang's fragMain: the heightfield terrain
    // (material.x >= 0.5) carries no normal attribute, so its face normal comes
    // from screen-space derivatives. Computed outside the branch - derivatives
    // in non-uniform control flow are undefined.
    float3 faceNormal = normalize(cross(ddx(worldPos), ddy(worldPos)));
    faceNormal = faceNormal.y < 0.0 ? -faceNormal : faceNormal;
    float3 n = ubo.material.x < 0.5 ? normalize(worldNormal) : faceNormal;
    // .a is the original's ray distance t. Background keeps the clear value
    // (a unit normal with a = -1), which is what makes silhouettes show up as
    // a large distance jump without also firing the normal term across open sky.
    return float4(n, length(mul(ubo.view, float4(worldPos, 1.0)).xyz));
}

// ---- outline ---------------------------------------------------------------

[[vk::binding(1, 0)]] Texture2D gbuffer;        // what the original called iChannel0
[[vk::binding(2, 0)]] SamplerState gbufferSampler;

struct OutlinePush {
    float2 iResolution;
    // The original's scene was ~5 world units across, so its raw distance deltas
    // landed either side of the 0.5 threshold below. Rescale here rather than
    // retuning the threshold. Nothing is measured; nudge it to taste.
    float distScale;
    float strength;
};
[[vk::push_constant]] OutlinePush pc;

// No vertex buffer bound for this draw: three vertices, indexed only by
// SV_VertexID, cover the whole viewport with one oversized triangle.
[shader("vertex")]
float4 outlineVert(uint vertexIndex : SV_VertexID) : SV_Position {
    float2 uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    return float4(uv * 2.0 - 1.0, 1.0, 1.0);
}

float4 tap(float2 uv) {
    return gbuffer.SampleLevel(gbufferSampler, uv, 0.0);
}

float2 getDeltas(float2 uv) {
    float2 pixel = 1.0 / pc.iResolution;
    float3 pole = float3(-1, 0, +1);
    float dpos = 0.0;
    float dnor = 0.0; // never written in the original either; kept so the
                      // composite below still reads like the source

    float4 s0 = tap(uv + pixel * pole.xx); // x1, y1
    float4 s1 = tap(uv + pixel * pole.yx); // x2, y1
    float4 s2 = tap(uv + pixel * pole.zx); // x3, y1
    float4 s3 = tap(uv + pixel * pole.xy); // x1, y2
    float4 s5 = tap(uv + pixel * pole.zy); // x3, y2
    float4 s6 = tap(uv + pixel * pole.xz); // x1, y3
    float4 s7 = tap(uv + pixel * pole.yz); // x2, y3
    float4 s8 = tap(uv + pixel * pole.zz); // x3, y3
    // s4, the centre tap, went unused in the original too.

    dpos = (
        abs(s1.a - s7.a) +
        abs(s5.a - s3.a) +
        abs(s0.a - s8.a) +
        abs(s2.a - s6.a)
    ) * 0.5 * pc.distScale;
    dpos += (
        max(0.0, 1.0 - dot(s1.rgb, s7.rgb)) +
        max(0.0, 1.0 - dot(s5.rgb, s3.rgb)) +
        max(0.0, 1.0 - dot(s0.rgb, s8.rgb)) +
        max(0.0, 1.0 - dot(s2.rgb, s6.rgb))
    );

    dpos = pow(max(dpos - 0.5, 0.0), 5.0);

    return float2(dpos, dnor);
}

[shader("fragment")]
float4 outlineFrag(float4 svPosition : SV_Position) : SV_Target {
    float2 deltas = getDeltas(svPosition.xy / pc.iResolution);
    // The original's `col -= deltas.x - deltas.y`. The blend does the subtract;
    // alpha is 0 so the reverse-subtract leaves the frame's alpha alone.
    return float4(((deltas.x - deltas.y) * pc.strength).xxx, 0.0);
}
