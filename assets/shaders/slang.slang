// Textured model: MVP transform in the vertex stage, shading in the fragment stage.
// Bindings and vertex locations must match Core::createDescriptorSetLayout and
// Vertex::getAttributeDescriptions in src/main.cpp.

// Must match MAX_LIGHTS in src/lib/common/vertex.hpp.
static const uint MAX_LIGHTS = 4;

// One LightingPlugin DirectionalLight. xyz used in both fields, w padding.
struct GPULight {
    float4 direction;
    float4 color;
};

struct UniformBufferObject {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    // x: 2 = debug overlay (flat vertex color, y = its alpha), 0 = textured mesh.
    float4 material;
    GPULight lights[MAX_LIGHTS];
    uint lightCount;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;
[[vk::binding(1, 0)]] Sampler2D texSampler;

// No sun shadow: it was an inline ray query against a TLAS at binding 2, which
// the host never bound and this hardware has no VK_KHR_ray_query for anyway.
// Everything below is lit unoccluded. A shadow map is the way back, not
// RayQuery.

// Tuning knobs. Nothing here is measured, so expect to nudge them.
static const float3 SKY_COLOR = float3(0.35, 0.48, 0.72);

struct VSOutput {
    float4 position : SV_Position;
    float3 color;
    float2 uv;
    float3 worldNormal;
};

// Parameters map to vertex input locations 0, 1, 2, 3 in declaration order.
[shader("vertex")]
VSOutput vertMain(float3 position, float3 color, float2 uv, float3 normal) {
    VSOutput output;
    float4 world = mul(ubo.model, float4(position, 1.0));
    output.position = mul(ubo.proj, mul(ubo.view, world));
    output.color = color;
    output.uv = uv;
    // Upper 3x3 of the model matrix: fine as long as scale stays uniform,
    // which is all Transform.scale is used for here today. A non-uniform
    // scale needs the inverse-transpose instead.
    float3x3 normalMatrix = float3x3(ubo.model[0].xyz, ubo.model[1].xyz, ubo.model[2].xyz);
    output.worldNormal = normalize(mul(normalMatrix, normal));
    return output;
}

// Narkowicz's ACES fit. The swapchain is B8G8R8A8_SRGB, so it encodes gamma for us
// and this must stay linear - do not add a pow(1/2.2) here.
float3 tonemapACES(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// The edges used to be found here with fwidth, for want of anything to sample
// neighbouring pixels from. outline.slang does it properly now - a real 3x3
// kernel over a normal+distance prepass - so nothing shades edges in this file.

[shader("fragment")]
float4 fragMain(float3 color, float2 uv, float3 worldNormal) : SV_Target {
    // Debug overlay (material.x == 2): flat vertex color, alpha from material.y.
    // No lighting, no texture, no shadow ray - it's a diagnostic, not a surface.
    if (ubo.material.x > 1.5) {
        return float4(color, ubo.material.y);
    }

    float3 n = normalize(worldNormal);
    float3 albedo = color * texSampler.Sample(uv).rgb;
    // Ambient from a sky gradient plus one unoccluded term per light.
    float3 lit = albedo * SKY_COLOR * (0.5 + 0.5 * n.y);
    for (uint i = 0; i < ubo.lightCount; i++) {
        lit += albedo * ubo.lights[i].color.xyz * saturate(dot(n, ubo.lights[i].direction.xyz));
    }
    return float4(tonemapACES(lit), 1.0);
}
