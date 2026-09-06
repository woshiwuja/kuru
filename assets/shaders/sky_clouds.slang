// Ported from a Shadertoy-style GLSL sky/cloud shader to Slang.
// Notes on the port:
//  - vec2/vec3/vec4/mat3 -> float2/float3/float4/float3x3
//  - GLSL texture(sampler, uv, bias) with a large negative bias forced
//    sampling the sharpest mip (level 0); replicated here with SampleLevel(uv, 0.0)
//  - GLSL's mod() differs from HLSL/Slang fmod() for negative inputs, so a
//    custom mod() is defined below to preserve the original behavior
//  - mix() -> lerp()

// Explicit bindings: RenderPlugin::createSkyDescriptorSetLayout builds the
// layout to match these indices directly, rather than reading them back out.
[[vk::binding(1, 0)]] Texture2D iChannel0;
[[vk::binding(2, 0)]] SamplerState iChannel0Sampler;

[[vk::binding(0, 0)]] cbuffer ShaderConstants
{
    float2   iResolution;
    float    iTime;
    float    _pad;
    // inverse(proj * rotation-only view): unprojects a screen pixel into a
    // world-space ray direction that follows camera rotation but not position.
    float4x4 invViewRotProj;
    // First DirectionalLight's color, from LightingPlugin. w unused.
    float4   sunColor;
};

// No vertex buffer bound for this draw: three vertices, indexed only by
// SV_VertexID, cover the whole viewport with one oversized triangle.
[shader("vertex")]
float4 vertMain(uint vertexIndex : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    return float4(uv * 2.0 - 1.0, 1.0, 1.0); // z = 1: the far plane, depth test is off anyway
}

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float y)
{
    return x - y * floor(x / y);
}

float noise(float3 x)
{
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);

    float2 uv = (p.xy + float2(37.0, 17.0) * p.z) + f.xy;
    float2 rg = iChannel0.SampleLevel(iChannel0Sampler, (uv + 0.5) / 256.0, 0.0).yx;
    return lerp(rg.x, rg.y, f.z);
}

float map(float3 p)
{
    float3 q = p + 0.2 * float3(3.0, 0.3, 5.0) * mod(iTime, 3600.0) * 2.0;
    float n = 0.0, f = 0.5;
    n += f * noise(q); q *= 3.001; f *= 0.333;
    n += f * noise(q); q *= 3.002; f *= 0.332;
    n += f * noise(q);
    return n;
}

float scene(float3 p)
{
    return p.y + 2.0 - 0.003 * map(float3(p.x, 0.0, p.z));
}

float3 sceneNormal(float3 p, float d)
{
    float e = 0.05;
    float dx = scene(float3(e, 0.0, 0.0) + p) - d;
    float dy = scene(float3(0.0, e, 0.0) + p) - d;
    float dz = scene(float3(0.0, 0.0, e) + p) - d;
    return normalize(float3(dx, dy, dz));
}

float3 shadeBg(float3 nml, float2 fragCoord)
{
    float2 aspect = float2(iResolution.x / iResolution.y, 1.0);
    float2 uv = (2.0 * fragCoord.xy / iResolution.xy - 1.0) * aspect;

    float3 bgLight = normalize(float3(
        sin(iTime * 0.1) * 0.1,
        cos(iTime * 0.1) * 0.6 - 0.3,
        -1.0
    ));

    float sunD = dot(bgLight, nml) > 0.995 ? 1.0 : 0.0;
    float3 sun = float3(6.5, 3.5, 2.0);
    float skyPow = dot(nml, float3(0.0, -1.0, 0.0));
    float centerPow = 0.0;
    float horizonPow = pow(1.0 - abs(skyPow), 3.0) * (5.0 + centerPow);
    float sunPow = dot(nml, bgLight);
    float sp = max(sunPow, 0.0);
    float scattering = clamp(1.0 - abs(2.0 * (-bgLight.y)), 0.0, 1.0);

    float3 bgCol = max(0.0, skyPow) * 2.0 * float3(0.8, 0.8, 0.8);
    bgCol += 0.5 * float3(0.8, 0.8, 0.8) * horizonPow;
    bgCol += sun * (sunD + pow(sp, max(128.0, abs(bgLight.y) * 512.0)));
    bgCol += float3(0.4, 0.2, 0.15) * (pow(sp, 8.0) + pow(sp, max(8.0, abs(bgLight.y) * 128.0)));
    bgCol *= lerp(float3(0.7, 0.85, 0.95), float3(1.0, 0.45, 0.1), scattering);
    bgCol *= 1.0 - clamp(bgLight.y * 3.0, 0.0, 0.6);

    float cloudFac = pow(abs(skyPow), 0.8);

    // Piecewise cloud-cover response curve, kept as-is from the original tuning.
    float ufCloudCover = 0.5;
    float cc = 0.0;
    if (ufCloudCover < 0.1) {
        cc = 0.0 + 0.2 * (ufCloudCover - 0.0) / 0.1;
    } else if (ufCloudCover < 0.2) {
        cc = 0.2 + 0.1 * (ufCloudCover - 0.1) / 0.1;
    } else if (ufCloudCover < 0.3) {
        cc = 0.3 + 0.1 * (ufCloudCover - 0.2) / 0.1;
    } else if (ufCloudCover < 0.5) {
        cc = 0.4 + 0.1 * (ufCloudCover - 0.3) / 0.3;
    } else if (ufCloudCover < 0.75) {
        cc = 0.5 + 0.2 * pow((ufCloudCover - 0.5) / 0.25, 2.0);
    } else {
        cc = 0.7 + 0.75 * pow((ufCloudCover - 0.75) / 0.25, 2.0);
    }

    float cloud = 0.0;
    cloud += min(1.0, 1.0 - smoothstep(0.0, cc, map(nml / nml.y))) * 0.1;
    cloud += min(1.0, 1.0 - smoothstep(0.0, cc, map(nml * 1.03 / nml.y))) * 0.1;
    cloud += min(1.0, 1.0 - smoothstep(0.0, cc, map(nml * 3.0 / nml.y))) * 0.1;
    bgCol *= 1.0 + cloudFac * cloud;

    return pow(max(float3(0.0, 0.0, 0.0), bgCol), float3(2.6, 2.6, 2.6));
}

float3x3 rotationXY(float2 angle)
{
    float cp = cos(angle.x);
    float sp = sin(angle.x);
    float cy = cos(angle.y);
    float sy = sin(angle.y);

    float3x3 rotZ = float3x3(
         cy, -sy, 0.0,
         sy,  cy, 0.0,
        0.0, 0.0, 1.0
    );
    float3x3 rotX = float3x3(
        cp, 0.0, -sp,
        0.0, 1.0,  0.0,
        sp, 0.0,  cp
    );
    return mul(rotZ, rotX);
}

struct FragmentInput
{
    float4 svPosition : SV_Position;
};

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    float2 fragCoord = input.svPosition.xy;

    float2 aspect = float2(iResolution.x / iResolution.y, 1.0);
    float2 uv = (2.0 * (fragCoord.xy / iResolution.xy) - 1.0) * aspect;

    // Real camera ray, not the original's fixed-forward assumption: this is
    // what makes the sky track the camera's look direction instead of the screen.
    float2 ndc = 2.0 * (fragCoord.xy / iResolution.xy) - 1.0;
    float4 worldDir = mul(invViewRotProj, float4(ndc, 1.0, 1.0));
    float3 d = normalize(worldDir.xyz);
    float3 p = float3(uv * -2.0, -9.5);
    float3 tr = float3(1.0, 1.0, 1.0);

    if (d.y < 0.0) {
        float dist = -2.0 / d.y - p.y / d.y; // p.y + d.y * dist = -2.0
        p += d * dist;
        float3 nml = sceneNormal(p, 0.0);
        float f = pow(1.0 - dot(d, -float3(0.0, 1.0, 0.0)), 5.0);
        nml = lerp(nml, float3(0.0, 1.0, 0.0), f);
        d = reflect(d, nml);
        tr *= lerp(0.5 * float3(0.5, 0.9, 0.75), float3(1.0, 1.0, 1.0), f);
    }

    float3 col = tr * shadeBg(-d, fragCoord);

    // Tint with the sun's color instead of the old dither noise tap.
    float4 fragColor = pow(
        float4(sunColor.rgb + (1.0 - exp(-1.3 * col.rgb)), 1.0),
        float4(1.3, 1.3, 1.3, 1.3)
    );

    return fragColor;
}
