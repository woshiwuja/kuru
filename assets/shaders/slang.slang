// Textured model: MVP transform in the vertex stage, shading in the fragment stage.
// Bindings and vertex locations must match Core::createDescriptorSetLayout and
// Vertex::getAttributeDescriptions in src/main.cpp.

// Must match MAX_LIGHTS in src/game/plugins/lighting/lighting.hpp.
static const uint MAX_LIGHTS = 4;

// float4 throughout, mirroring lighting.hpp: a float3 here would take a
// 16-byte slot but only 12 bytes of size, which is exactly what the C++ side
// cannot reproduce with packed glm::vec3s. The w lanes carry the scalars.
struct DirectionalLight {
    float4 direction;   // xyz: direction, w unused
    float4 color;
};
struct PointLight {
    float4 position;    // xyz: world position, w: radius
    float4 color;
};
struct SpotLight {
    float4 position;    // xyz: world position, w: near_radius
    float4 direction;   // xyz: beam direction, w: far_radius
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
    DirectionalLight dirLights[MAX_LIGHTS];
    PointLight       pointLights[MAX_LIGHTS];
    SpotLight        spotLights[MAX_LIGHTS];
    // x: directional, y: point, z: spot. w unused.
    uint4 counts;
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

// Props (RenderPlugin::propPipeline): the model matrix comes per instance from
// vertex binding 1 as glm's four columns, locations 4-7. ubo.model is unused.
// Building a float4x4 from columns makes them its rows, so the vector goes on
// the left: mul(v, M) = sum v[i] * c[i], which is glm's M * v.
[shader("vertex")]
VSOutput vertMainInstanced(float3 position, float3 color, float2 uv, float3 normal,
                           float4 c0, float4 c1, float4 c2, float4 c3) {
    float4x4 model = float4x4(c0, c1, c2, c3);
    VSOutput output;
    float4 world = mul(float4(position, 1.0), model);
    output.position = mul(ubo.proj, mul(ubo.view, world));
    output.color = color;
    output.uv = uv;
    // Same uniform-scale caveat as vertMain.
    output.worldNormal = normalize(mul(normal, float3x3(c0.xyz, c1.xyz, c2.xyz)));
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

// Inverse-square, windowed so the light reaches exactly zero at `radius`
// instead of dragging a faint tail across the whole scene. The +1 keeps the
// denominator off zero at the light's own position.
float windowedFalloff(float dist, float radius) {
    float f = saturate(1.0 - pow(dist / max(radius, 1e-3), 4.0));
    return f * f / (dist * dist + 1.0);
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
    // viewDir is cameraPos - worldPos unnormalized, so the fragment's world
    // position comes back for free - no extra varying needed for the
    // positional lights below.
    float3 worldPos = ubo.cameraPos.xyz - viewDir;

    for (uint i = 0; i < ubo.counts.x; i++) {
        float3 l = normalize(ubo.dirLights[i].direction.xyz);
        lit += ubo.dirLights[i].color.rgb * brdf(n, v, l, albedo, metallic, roughness);
    }
    for (uint i = 0; i < ubo.counts.y; i++) {
        float3 d = ubo.pointLights[i].position.xyz - worldPos;
        float dist = length(d);
        float atten = windowedFalloff(dist, ubo.pointLights[i].position.w);
        if (atten <= 0.0) { continue; }
        lit += ubo.pointLights[i].color.rgb * atten *
               brdf(n, v, d / max(dist, 1e-4), albedo, metallic, roughness);
    }
    for (uint i = 0; i < ubo.counts.z; i++) {
        float3 d = ubo.spotLights[i].position.xyz - worldPos;
        float dist = length(d);
        float3 l = d / max(dist, 1e-4);
        // near_radius / far_radius are the cone's radii at unit distance, i.e.
        // tan(half angle), so cos(half angle) = 1 / sqrt(1 + r*r). Inside the
        // near cone is full bright, past the far cone is dark.
        float cd = dot(-l, normalize(ubo.spotLights[i].direction.xyz));
        float cosInner = rsqrt(1.0 + ubo.spotLights[i].position.w * ubo.spotLights[i].position.w);
        float cosOuter = rsqrt(1.0 + ubo.spotLights[i].direction.w * ubo.spotLights[i].direction.w);
        float cone = smoothstep(cosOuter, cosInner, cd);
        if (cone <= 0.0) { continue; }
        // ponytail: SpotLight has no range field, so distance is pure
        // inverse-square. Add one and reuse windowedFalloff if spots need to
        // stop at a set distance like point lights do.
        lit += ubo.spotLights[i].color.rgb * cone / (dist * dist + 1.0) *
               brdf(n, v, l, albedo, metallic, roughness);
    }
    return float4(tonemapACES(lit), 1.0);
}
