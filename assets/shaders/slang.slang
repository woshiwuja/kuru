// Textured model: MVP transform in the vertex stage, shading in the fragment stage.
// Bindings and vertex locations must match Core::createDescriptorSetLayout and
// Vertex::getAttributeDescriptions in src/main.cpp.

// Must match MAX_LIGHTS in src/lib/common/vertex.hpp.
static const uint MAX_LIGHTS = 4;

struct DirectionalLight {
    float4 direction;
    float4 color;
};
struct PointLight{
    float radius;
    float4 color;
};

struct UniformBufferObject {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    // x: 2 = debug overlay (flat vertex color, y = its alpha), 0 = textured mesh.
    float4 material;
    float4 baseColor;
    float4 pbr;
    float4 cameraPos;
    DirectionalLight lights[MAX_LIGHTS];
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
    // Camera minus fragment, unnormalized.
    float3 viewDir;
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
    output.viewDir = ubo.cameraPos.xyz - world.xyz;
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

static const float PI = 3.14159265;

// Cook-Torrance GGX for one direct light: GGX normal distribution, Schlick-GGX
// geometry with the direct-lighting k, Schlick Fresnel. Returns radiance already
// multiplied by N.L, so the caller only scales by light color.
float3 brdf(float3 n, float3 v, float3 l, float3 albedo, float metallic, float roughness) {
    float3 h = normalize(v + l);
    // Clamped away from 0: a perfect mirror is a delta function no point light can hit.
    float a  = max(roughness * roughness, 1e-3);
    float a2 = a * a;
    float ndoth = saturate(dot(n, h));
    float ndotv = saturate(dot(n, v)) + 1e-4;
    float ndotl = saturate(dot(n, l));

    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom);

    float k = a * 0.5;
    float G = (ndotv / (ndotv * (1.0 - k) + k)) * (ndotl / (ndotl * (1.0 - k) + k));

    float3 f0 = lerp(float3(0.04), albedo, metallic);
    float3 F  = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(h, v)), 5.0);

    float3 spec = D * G * F / (4.0 * ndotv * ndotl + 1e-4);
    // Metals have no diffuse lobe, and what Fresnel reflects is not refracted.
    float3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * ndotl;
}

[shader("fragment")]
float4 fragMain(float3 color, float2 uv, float3 worldNormal, float3 viewDir) : SV_Target {
    // Debug overlay (material.x == 2): flat vertex color, alpha from material.y.
    // No lighting, no texture, no shadow ray - it's a diagnostic, not a surface.
    if (ubo.material.x > 1.5) {
        return float4(color, ubo.material.y);
    }

    float4 tex = texSampler.Sample(uv);
    // glTF alpha mask: binary, no blending. material.w gates it, material.z is the cutoff.
    if (ubo.material.w > 0.5 && tex.a * ubo.baseColor.a < ubo.material.z) {
        discard;
    }

    float3 n = normalize(worldNormal);
    float3 v = normalize(viewDir);
    float3 albedo = color * ubo.baseColor.rgb * tex.rgb;
    float metallic  = saturate(ubo.pbr.x);
    float roughness = clamp(ubo.pbr.y, 0.04, 1.0);

    // Stand-in for an IBL: the sky gradient lights the diffuse lobe, and a
    // roughness-faded f0 term keeps metals reflecting sky instead of going black.
    // Replace both with a prefiltered environment map when there is one.
    float3 f0 = lerp(float3(0.04), albedo, metallic);
    float3 sky = SKY_COLOR * (0.5 + 0.5 * n.y);
    float3 lit = sky * (albedo * (1.0 - metallic) + f0 * (1.0 - roughness));
    for (uint i = 0; i < ubo.lightCount; i++) {
        float3 l = normalize(ubo.lights[i].direction.xyz);
        lit += ubo.lights[i].color.xyz * brdf(n, v, l, albedo, metallic, roughness);
    }
    return float4(tonemapACES(lit), 1.0);
}
