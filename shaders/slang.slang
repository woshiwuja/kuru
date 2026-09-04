// Textured model: MVP transform in the vertex stage, shading in the fragment stage.
// Bindings and vertex locations must match Core::createDescriptorSetLayout and
// Vertex::getAttributeDescriptions in src/main.cpp.
//
// Terrain shading is the height/slope material stack from Fewes' raymarched terrain
// (https://www.shadertoy.com/view/7ljcRW), applied to the rasterised heightfield mesh.
// The raymarching, erosion simulation, water and atmosphere are not here - they need
// offscreen passes this renderer does not have yet.

struct UniformBufferObject {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    // x: 1 = procedural terrain, 0 = plain textured mesh. y, z: world height range.
    float4 material;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;
[[vk::binding(1, 0)]] Sampler2D texSampler;

// Tuning knobs. Nothing here is measured, so expect to nudge them.
static const float3 SUN_DIR      = normalize(float3(-1.0, 0.4, 0.05));
static const float3 SUN_COLOR    = float3(1.7, 1.5, 1.2);
static const float3 SKY_COLOR    = float3(0.35, 0.48, 0.72);
static const float3 CLIFF_COLOR  = float3(0.30, 0.28, 0.26);
static const float3 DIRT_COLOR   = float3(0.26, 0.19, 0.12);
static const float3 SAND_COLOR   = float3(0.62, 0.55, 0.38);
static const float3 GRASS_COLOR1 = float3(0.11, 0.19, 0.07);
static const float3 GRASS_COLOR2 = float3(0.23, 0.29, 0.11);
static const float3 SNOW_COLOR   = float3(0.90, 0.92, 0.95);

struct VSOutput {
    float4 position : SV_Position;
    float3 color;
    float2 uv;
    float3 worldPos;
};

// Parameters map to vertex input locations 0, 1, 2 in declaration order.
[shader("vertex")]
VSOutput vertMain(float3 position, float3 color, float2 uv) {
    VSOutput output;
    float4 world = mul(ubo.model, float4(position, 1.0));
    output.position = mul(ubo.proj, mul(ubo.view, world));
    output.worldPos = world.xyz;
    output.color = color;
    output.uv = uv;
    return output;
}

// Narkowicz's ACES fit. The swapchain is B8G8R8A8_SRGB, so it encodes gamma for us
// and this must stay linear - do not add a pow(1/2.2) here.
float3 tonemapACES(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

[shader("fragment")]
float4 fragMain(float3 color, float2 uv, float3 worldPos) : SV_Target {
    // The mesh carries no normal attribute, so take the face normal from screen-space
    // derivatives. Must be outside any branch: derivatives in non-uniform control flow
    // are undefined. Faceted, but at 257x257 the facets are sub-pixel at distance.
    float3 normal = normalize(cross(ddx(worldPos), ddy(worldPos)));

    // material.x comes from the UBO, so this branch is uniform across the draw and
    // sampling inside it is safe.
    if (ubo.material.x < 0.5) {
        return float4(color * texSampler.Sample(uv).rgb, 1.0);
    }

    normal = normal.y < 0.0 ? -normal : normal;   // a heightfield always faces up

    float h = saturate((worldPos.y - ubo.material.y) / max(1e-4, ubo.material.z - ubo.material.y));
    float slope = 1.0 - normal.y;

    // The original perturbs these thresholds with a detail texture. Without one they are
    // clean bands; a procedural noise on h and slope is the upgrade if that reads too neatly.
    float3 diffuse = lerp(GRASS_COLOR1, GRASS_COLOR2, smoothstep(0.35, 0.65, h));
    diffuse = lerp(diffuse, SAND_COLOR, smoothstep(0.12, 0.02, h));
    diffuse = lerp(diffuse, CLIFF_COLOR, smoothstep(0.35, 0.62, slope));
    diffuse = lerp(diffuse, DIRT_COLOR, smoothstep(0.20, 0.45, slope) * smoothstep(0.75, 0.30, h));
    diffuse = lerp(diffuse, SNOW_COLOR, smoothstep(0.82, 0.93, h) * smoothstep(0.55, 0.20, slope));

    // Ambient from a sky gradient, direct sun, and a crude single bounce.
    float3 lit = diffuse * SKY_COLOR * (0.5 + 0.5 * normal.y);
    lit += diffuse * SUN_COLOR * saturate(dot(normal, SUN_DIR));
    lit += diffuse * SUN_COLOR * saturate(dot(normal, SUN_DIR * float3(1.0, -1.0, 1.0))) * 0.15;

    return float4(tonemapACES(lit), 1.0);
}
