// Parallax occlusion mapping for entities with a Parallax component
// (src/game/plugins/parallax). Reuses slang.slang wholesale: vertMain for the
// geometry and fragMain for the lighting, fed a shifted uv, a normal bent by
// the height, and a self-shadow term.
#include "slang.slang"

// Matches Parallax in parallax.hpp.
struct ParallaxPush {
    float depth;      // depth of the relief, in metres
    uint  steps;      // ray-march layers at grazing angles (a quarter head-on)
    uint  invert;     // 1: dark = raised
    float bump;       // lighting normal strength
    float shadow;     // self-shadow strength, 0 = off
    float fadeDist;   // metres: fades out over the last quarter, flat past it
    uint  silhouette; // 1: discard where the relief leaves uv [0, 1]
};
[[vk::push_constant]] ConstantBuffer<ParallaxPush> parallax;

// ParallaxPlugin's own set: <texture>_height.png, or the albedo when there is none.
[[vk::binding(0, 1)]] Sampler2D heightMap;

// 0 = top of the relief, 1 = bottom.
float depthAt(float2 uv, float2 dx, float2 dy) {
    // PNGs load as sRGB, so the hardware linearized the height: undo it.
    float h = pow(dot(heightMap.SampleGrad(uv, dx, dy).rgb, float3(0.299, 0.587, 0.114)),
                  1.0 / 2.2);
    if (parallax.invert != 0) {
        return h;
    }
    return 1.0 - h;
}

[shader("fragment")]
float4 parallaxFrag(float3 color, float2 uv, float3 worldNormal, float3 viewDir) : SV_Target {
    // Past fadeDist: plain shading, the march would be sub-pixel anyway.
    float fade = saturate((parallax.fadeDist - length(viewDir)) / (0.25 * parallax.fadeDist));
    if (fade <= 0.0) {
        return fragMain(color, uv, worldNormal, viewDir);
    }
    // No tangents in Vertex: rebuild the frame from screen-space derivatives
    // (Schüler's cotangent frame).
    float3 n = normalize(worldNormal);
    float3 dp1 = ddx(-viewDir), dp2 = ddy(-viewDir);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    t *= invMax;
    b *= invMax;
    // Metres -> uv: the surface's metres per uv unit, from the same
    // derivatives (area in world / area in uv). Keeps the relief the same
    // physical depth on any mesh, however its uvs are scaled.
    float uvArea = abs(duv1.x * duv2.y - duv1.y * duv2.x);
    float metresPerUv = sqrt(length(cross(dp1, dp2)) / max(uvArea, 1e-20));
    float scale = parallax.depth / max(metresPerUv, 1e-6) * fade;

    float3 v = normalize(viewDir);
    float3 vt = float3(dot(t, v), dot(b, v), dot(n, v));

    // Steep parallax: step along the view ray until it dips under the surface.
    // Head-on needs far fewer layers than grazing.
    float maxLayers = float(max(parallax.steps, 1u));
    uint layers = uint(lerp(maxLayers, max(maxLayers * 0.25, 1.0), saturate(vt.z)));
    float layerDepth = 1.0 / float(layers);
    float2 delta = vt.xy / max(vt.z, 0.1) * scale / float(layers);
    float2 cur = uv;
    float rayDepth = 0.0;
    float surface = depthAt(cur, duv1, duv2);
    for (uint i = 0; i < layers && rayDepth < surface; i++) {
        cur -= delta;
        rayDepth += layerDepth;
        surface = depthAt(cur, duv1, duv2);
    }
    // Lerp between the last two steps for a smooth hit instead of terraces.
    float after = surface - rayDepth;
    float before = depthAt(cur + delta, duv1, duv2) - rayDepth + layerDepth;
    float w = after / min(after - before, -1e-5);
    cur = lerp(cur, cur + delta, saturate(w));

    // Silhouette: a ray that left the mesh's uv square looked past its edge.
    // ponytail: assumes uv in [0, 1] per face; a tiled mesh needs it off.
    if (parallax.silhouette != 0 && (any(cur < 0.0) || any(cur > 1.0))) {
        discard;
    }

    // Bend the normal with the height slope at the hit, one texel each way,
    // so the light shades the relief instead of a flat wall.
    uint tw, th;
    heightMap.GetDimensions(tw, th);
    float2 e = 1.0 / float2(tw, th);
    float du = depthAt(cur + float2(e.x, 0), duv1, duv2) - depthAt(cur - float2(e.x, 0), duv1, duv2);
    float dv = depthAt(cur + float2(0, e.y), duv1, duv2) - depthAt(cur - float2(0, e.y), duv1, duv2);
    // Height = -depth * scale, slope per uv = d / (2e); the normal leans
    // against the slope.
    float2 slope = float2(du / (2.0 * e.x), dv / (2.0 * e.y)) * scale * parallax.bump;
    float3 bent = normalize(normalize(t) * slope.x + normalize(b) * slope.y + n);

    // Self-shadow from dirLights[0]: march from the hit back up toward the
    // light; anything higher on the way blocks it, softly by how much.
    float lit = 1.0;
    if (ubo.counts.x > 0 && parallax.shadow > 0.0) {
        float3 l = normalize(ubo.dirLights[0].direction.xyz);
        float3 lt = float3(dot(t, l), dot(b, l), dot(n, l));
        float hit = depthAt(cur, duv1, duv2);
        if (lt.z > 0.0 && hit > 0.0) {
            uint sLayers = max(layers / 2, 1u);
            float sStep = hit / float(sLayers);
            float2 sDelta = lt.xy / max(lt.z, 0.1) * scale * sStep;
            float2 suv = cur;
            float d = hit;
            float occluded = 0.0;
            for (uint i = 0; i < sLayers; i++) {
                suv += sDelta;
                d -= sStep;
                occluded = max(occluded, d - depthAt(suv, duv1, duv2));
            }
            // ponytail: scales the vertex colour, so it darkens the sky term
            // too (reads as cavity AO). Split fragMain's light loop if the
            // sun alone should be shadowed.
            lit = 1.0 - saturate(occluded * parallax.shadow * fade);
        }
    }

    return fragMain(color * lit, cur, bent, viewDir);
}
