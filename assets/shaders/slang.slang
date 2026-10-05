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
    float4 color;       // rgb: color, w: intensity
};
struct SpotLight {
    float4 position;    // xyz: world position, w: near_radius
    float4 direction;   // xyz: beam direction, w: far_radius
    float4 color;       // rgb: color, w: intensity
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
    // Last, so outline.slang's shorter copy of this struct stays a valid prefix.
    // World -> shadow map clip space for dirLights[0].
    float4x4 lightViewProj;
    // x: 1 if the shadow map holds dirLights[0] this frame, y: texel size
    // (1 / resolution), z: depth bias. w unused.
    float4 shadow;
    // Spot and point lights: layers of localShadowMap. Spot i is layer i,
    // point i face f is layer MAX_LIGHTS + 6i + f, with matrix pointViewProj[6i + f].
    float4x4 spotViewProj[MAX_LIGHTS];
    float4x4 pointViewProj[MAX_LIGHTS * 6];
    // x: spot lights with a rendered layer, y: point lights with rendered
    // faces (a light added mid-frame has none yet), z: texel size, w: depth bias.
    float4 localShadow;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;
[[vk::binding(1, 0)]] Sampler2D texSampler;
// Depth of the scene seen from dirLights[0] (RenderPlugin::drawShadowPass),
// read through a comparison sampler: each tap returns the 0..1 fraction of
// its 2x2 texels that are lit.
[[vk::binding(2, 0)]] Sampler2DShadow shadowMap;
[[vk::binding(3, 0)]] Sampler2DArrayShadow localShadowMap;

// The shadow pass's matrix (sun, a spot, or one point light face), per draw.
struct ShadowPush {
    float4x4 viewProj;
};
[[vk::push_constant]] ConstantBuffer<ShadowPush> shadowPush;

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

// Shadow pass: depth only, no fragment stage. Same per-entity UBO as vertMain;
// which light/face it renders for comes in as a push constant.
[shader("vertex")]
float4 shadowVert(float3 position, float3 color, float2 uv, float3 normal) : SV_Position {
    return mul(shadowPush.viewProj, mul(ubo.model, float4(position, 1.0)));
}

// Props in the shadow pass: the instance matrix, as in vertMainInstanced.
[shader("vertex")]
float4 shadowVertInstanced(float3 position, float3 color, float2 uv, float3 normal,
                           float4 c0, float4 c1, float4 c2, float4 c3) : SV_Position {
    float4 world = mul(float4(position, 1.0), float4x4(c0, c1, c2, c3));
    return mul(shadowPush.viewProj, world);
}

// Same 3x3 compare as sunShadow, against one layer of the local array.
// Outside the layer's frustum counts as lit.
float localShadow(float3 worldPos, float4x4 viewProj, uint layer) {
    float4 clip = mul(viewProj, float4(worldPos, 1.0));
    if (clip.w <= 0.0) { return 1.0; }
    float3 ndc = clip.xyz / clip.w;
    if (ndc.z > 1.0 || any(abs(ndc.xy) > 1.0)) { return 1.0; }
    float2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z - ubo.localShadow.w;
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += localShadowMap.SampleCmpLevelZero(
                float3(uv + float2(x, y) * ubo.localShadow.z, layer), depth);
        }
    }
    return lit / 9.0;
}

float spotShadow(float3 worldPos, uint i) {
    if (float(i) >= ubo.localShadow.x) { return 1.0; }
    return localShadow(worldPos, ubo.spotViewProj[i], i);
}

// The face is the dominant axis of light -> fragment, in the order
// +X -X +Y -Y +Z -Z (RenderPlugin::cubeFaces).
float pointShadow(float3 worldPos, uint i) {
    if (float(i) >= ubo.localShadow.y) { return 1.0; }
    float3 d = worldPos - ubo.pointLights[i].position.xyz;
    float3 a = abs(d);
    uint face;
    if (a.x >= a.y && a.x >= a.z) { face = d.x > 0.0 ? 0 : 1; }
    else if (a.y >= a.z)          { face = d.y > 0.0 ? 2 : 3; }
    else                          { face = d.z > 0.0 ? 4 : 5; }
    return localShadow(worldPos, ubo.pointViewProj[i * 6 + face],
                       MAX_LIGHTS + i * 6 + face);
}

// 1 = lit by dirLights[0], 0 = something sits between it and the light.
// 3x3 taps of the hardware 2x2 compare: soft-ish edges instead of stairs.
float sunShadow(float3 worldPos) {
    if (ubo.shadow.x < 0.5) { return 1.0; }
    float4 clip = mul(ubo.lightViewProj, float4(worldPos, 1.0));
    float3 ndc = clip.xyz / clip.w;
    // Past the far plane or outside the box: not covered, so not shadowed.
    if (ndc.z > 1.0 || any(abs(ndc.xy) > 1.0)) { return 1.0; }
    float2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z - ubo.shadow.z;
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += shadowMap.SampleCmpLevelZero(uv + float2(x, y) * ubo.shadow.y, depth);
        }
    }
    return lit / 9.0;
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

// Kulla-Conty multiple scattering. Single-scatter GGX loses the energy that
// bounces between microfacets more than once, so rough surfaces (metals most
// visibly) come out too dark. Kulla & Conty 2017 add it back as a second lobe
// built from E(mu), the single-scatter directional albedo, and its average
// E_avg. Both are LUTs, baked into this file by tools/kulla_conty_lut.py
// rather than bound as a texture.

// Tables: KC_E is 32x32, rows by roughness, columns by sqrt(mu) (packs the
// steep grazing end; bilinear error < 0.006). KC_EAVG is per roughness.
// BEGIN kulla_conty_lut.py
static const float KC_E[1024] = {
    0.6261, 0.6272, 0.7985, 0.9009, 0.9431, 0.9634, 0.9748, 0.9817, 0.9861, 0.9893, 0.9915, 0.9931, 0.9944, 0.9953, 0.9961, 0.9968, 0.9973, 0.9977, 0.9981, 0.9984, 0.9986, 0.9988, 0.9990, 0.9992, 0.9993, 0.9995, 0.9996, 0.9997, 0.9998, 0.9999, 0.9999, 1.0000,
    0.6253, 0.6261, 0.7923, 0.8972, 0.9409, 0.9620, 0.9738, 0.9810, 0.9856, 0.9889, 0.9911, 0.9928, 0.9941, 0.9951, 0.9959, 0.9966, 0.9972, 0.9976, 0.9980, 0.9983, 0.9986, 0.9988, 0.9990, 0.9992, 0.9993, 0.9994, 0.9996, 0.9997, 0.9998, 0.9999, 0.9999, 1.0000,
    0.7016, 0.6982, 0.6276, 0.7019, 0.7944, 0.8590, 0.8998, 0.9261, 0.9437, 0.9560, 0.9650, 0.9717, 0.9768, 0.9808, 0.9840, 0.9865, 0.9886, 0.9904, 0.9919, 0.9932, 0.9942, 0.9951, 0.9959, 0.9966, 0.9972, 0.9978, 0.9982, 0.9987, 0.9991, 0.9994, 0.9997, 1.0000,
    0.7656, 0.7630, 0.6540, 0.6299, 0.6727, 0.7381, 0.7981, 0.8446, 0.8790, 0.9042, 0.9231, 0.9374, 0.9485, 0.9572, 0.9642, 0.9699, 0.9746, 0.9785, 0.9818, 0.9846, 0.9870, 0.9890, 0.9908, 0.9924, 0.9937, 0.9949, 0.9961, 0.9970, 0.9979, 0.9985, 0.9992, 0.9999,
    0.7989, 0.7971, 0.7029, 0.6424, 0.6331, 0.6618, 0.7089, 0.7587, 0.8030, 0.8395, 0.8687, 0.8919, 0.9102, 0.9250, 0.9370, 0.9468, 0.9550, 0.9619, 0.9675, 0.9725, 0.9766, 0.9803, 0.9834, 0.9862, 0.9884, 0.9905, 0.9926, 0.9943, 0.9958, 0.9972, 0.9985, 0.9997,
    0.8180, 0.8167, 0.7425, 0.6759, 0.6403, 0.6371, 0.6584, 0.6939, 0.7345, 0.7740, 0.8093, 0.8396, 0.8649, 0.8860, 0.9035, 0.9180, 0.9303, 0.9406, 0.9494, 0.9568, 0.9628, 0.9686, 0.9735, 0.9778, 0.9817, 0.9851, 0.9881, 0.9908, 0.9933, 0.9954, 0.9974, 0.9992,
    0.8302, 0.8293, 0.7714, 0.7096, 0.6641, 0.6418, 0.6416, 0.6585, 0.6866, 0.7200, 0.7545, 0.7872, 0.8168, 0.8427, 0.8651, 0.8843, 0.9006, 0.9141, 0.9264, 0.9371, 0.9463, 0.9544, 0.9615, 0.9676, 0.9731, 0.9780, 0.9823, 0.9861, 0.9895, 0.9927, 0.9955, 0.9981,
    0.8385, 0.8378, 0.7924, 0.7383, 0.6912, 0.6595, 0.6451, 0.6465, 0.6606, 0.6836, 0.7116, 0.7417, 0.7713, 0.7991, 0.8241, 0.8465, 0.8670, 0.8850, 0.9008, 0.9146, 0.9267, 0.9371, 0.9464, 0.9547, 0.9621, 0.9688, 0.9746, 0.9798, 0.9847, 0.9890, 0.9928, 0.9964,
    0.8442, 0.8437, 0.8076, 0.7613, 0.7165, 0.6811, 0.6585, 0.6490, 0.6514, 0.6635, 0.6827, 0.7065, 0.7321, 0.7582, 0.7849, 0.8098, 0.8328, 0.8536, 0.8721, 0.8887, 0.9037, 0.9170, 0.9289, 0.9396, 0.9490, 0.9575, 0.9651, 0.9719, 0.9780, 0.9837, 0.9888, 0.9935,
    0.8480, 0.8475, 0.8185, 0.7792, 0.7383, 0.7025, 0.6757, 0.6591, 0.6528, 0.6556, 0.6658, 0.6814, 0.7023, 0.7261, 0.7509, 0.7755, 0.7989, 0.8214, 0.8422, 0.8614, 0.8789, 0.8946, 0.9088, 0.9218, 0.9334, 0.9437, 0.9532, 0.9619, 0.9696, 0.9768, 0.9833, 0.9891,
    0.8500, 0.8497, 0.8260, 0.7927, 0.7561, 0.7218, 0.6932, 0.6725, 0.6600, 0.6553, 0.6574, 0.6677, 0.6830, 0.7019, 0.7230, 0.7454, 0.7684, 0.7910, 0.8127, 0.8332, 0.8523, 0.8701, 0.8864, 0.9012, 0.9149, 0.9276, 0.9389, 0.9494, 0.9588, 0.9676, 0.9756, 0.9829,
    0.8506, 0.8504, 0.8308, 0.8025, 0.7702, 0.7381, 0.7095, 0.6865, 0.6699, 0.6596, 0.6577, 0.6621, 0.6715, 0.6850, 0.7020, 0.7213, 0.7420, 0.7633, 0.7844, 0.8053, 0.8253, 0.8441, 0.8619, 0.8789, 0.8944, 0.9087, 0.9219, 0.9342, 0.9455, 0.9558, 0.9654, 0.9742,
    0.8499, 0.8497, 0.8334, 0.8093, 0.7808, 0.7513, 0.7237, 0.6998, 0.6806, 0.6680, 0.6621, 0.6616, 0.6658, 0.6747, 0.6871, 0.7026, 0.7201, 0.7391, 0.7588, 0.7787, 0.7984, 0.8182, 0.8370, 0.8546, 0.8715, 0.8874, 0.9024, 0.9162, 0.9292, 0.9412, 0.9524, 0.9627,
    0.8479, 0.8477, 0.8341, 0.8134, 0.7884, 0.7616, 0.7354, 0.7116, 0.6916, 0.6778, 0.6686, 0.6641, 0.6644, 0.6689, 0.6773, 0.6885, 0.7026, 0.7186, 0.7358, 0.7543, 0.7734, 0.7922, 0.8110, 0.8293, 0.8473, 0.8641, 0.8802, 0.8955, 0.9099, 0.9235, 0.9362, 0.9480,
    0.8447, 0.8445, 0.8330, 0.8152, 0.7933, 0.7691, 0.7447, 0.7215, 0.7024, 0.6871, 0.6756, 0.6685, 0.6655, 0.6664, 0.6707, 0.6784, 0.6888, 0.7014, 0.7164, 0.7325, 0.7494, 0.7674, 0.7856, 0.8036, 0.8214, 0.8390, 0.8559, 0.8721, 0.8876, 0.9024, 0.9164, 0.9297,
    0.8402, 0.8401, 0.8302, 0.8149, 0.7956, 0.7740, 0.7514, 0.7298, 0.7113, 0.6953, 0.6825, 0.6734, 0.6678, 0.6655, 0.6667, 0.6709, 0.6777, 0.6876, 0.6993, 0.7127, 0.7278, 0.7439, 0.7604, 0.7777, 0.7951, 0.8124, 0.8295, 0.8462, 0.8624, 0.8782, 0.8932, 0.9076,
    0.8345, 0.8344, 0.8260, 0.8127, 0.7957, 0.7763, 0.7556, 0.7360, 0.7180, 0.7019, 0.6885, 0.6778, 0.6701, 0.6656, 0.6639, 0.6650, 0.6693, 0.6758, 0.6845, 0.6953, 0.7078, 0.7214, 0.7365, 0.7521, 0.7684, 0.7849, 0.8016, 0.8182, 0.8346, 0.8507, 0.8664, 0.8816,
    0.8275, 0.8274, 0.8202, 0.8086, 0.7937, 0.7763, 0.7575, 0.7397, 0.7224, 0.7066, 0.6928, 0.6812, 0.6720, 0.6655, 0.6614, 0.6605, 0.6618, 0.6655, 0.6714, 0.6795, 0.6892, 0.7006, 0.7132, 0.7270, 0.7415, 0.7569, 0.7725, 0.7885, 0.8045, 0.8204, 0.8362, 0.8517,
    0.8193, 0.8192, 0.8129, 0.8028, 0.7896, 0.7740, 0.7573, 0.7408, 0.7245, 0.7092, 0.6953, 0.6830, 0.6729, 0.6647, 0.6593, 0.6560, 0.6548, 0.6561, 0.6595, 0.6647, 0.6719, 0.6807, 0.6909, 0.7024, 0.7150, 0.7284, 0.7426, 0.7573, 0.7723, 0.7876, 0.8029, 0.8183,
    0.8097, 0.8096, 0.8041, 0.7953, 0.7836, 0.7696, 0.7548, 0.7395, 0.7242, 0.7095, 0.6956, 0.6831, 0.6722, 0.6632, 0.6561, 0.6509, 0.6480, 0.6471, 0.6479, 0.6508, 0.6553, 0.6615, 0.6692, 0.6783, 0.6885, 0.6999, 0.7121, 0.7251, 0.7386, 0.7527, 0.7670, 0.7816,
    0.7989, 0.7988, 0.7940, 0.7862, 0.7758, 0.7632, 0.7500, 0.7359, 0.7216, 0.7075, 0.6939, 0.6813, 0.6698, 0.6602, 0.6518, 0.6454, 0.6407, 0.6376, 0.6365, 0.6370, 0.6391, 0.6428, 0.6480, 0.6546, 0.6624, 0.6713, 0.6813, 0.6922, 0.7038, 0.7162, 0.7290, 0.7423,
    0.7868, 0.7868, 0.7825, 0.7756, 0.7663, 0.7551, 0.7431, 0.7301, 0.7167, 0.7032, 0.6900, 0.6774, 0.6659, 0.6555, 0.6463, 0.6387, 0.6324, 0.6278, 0.6247, 0.6231, 0.6229, 0.6243, 0.6271, 0.6311, 0.6364, 0.6429, 0.6504, 0.6589, 0.6683, 0.6785, 0.6894, 0.7008,
    0.7735, 0.7735, 0.7697, 0.7635, 0.7552, 0.7452, 0.7342, 0.7222, 0.7097, 0.6968, 0.6841, 0.6716, 0.6600, 0.6490, 0.6393, 0.6306, 0.6232, 0.6171, 0.6122, 0.6087, 0.6066, 0.6057, 0.6061, 0.6078, 0.6106, 0.6146, 0.6195, 0.6256, 0.6325, 0.6402, 0.6488, 0.6580,
    0.7591, 0.7591, 0.7557, 0.7502, 0.7426, 0.7336, 0.7235, 0.7124, 0.7006, 0.6884, 0.6760, 0.6639, 0.6521, 0.6410, 0.6307, 0.6211, 0.6127, 0.6053, 0.5990, 0.5938, 0.5898, 0.5869, 0.5851, 0.5845, 0.5849, 0.5864, 0.5888, 0.5923, 0.5966, 0.6018, 0.6078, 0.6145,
    0.7437, 0.7436, 0.7406, 0.7355, 0.7286, 0.7205, 0.7112, 0.7009, 0.6897, 0.6781, 0.6660, 0.6542, 0.6424, 0.6311, 0.6203, 0.6102, 0.6009, 0.5923, 0.5848, 0.5781, 0.5723, 0.5677, 0.5639, 0.5611, 0.5593, 0.5584, 0.5584, 0.5593, 0.5610, 0.5636, 0.5669, 0.5710,
    0.7272, 0.7272, 0.7244, 0.7198, 0.7134, 0.7060, 0.6974, 0.6877, 0.6771, 0.6659, 0.6544, 0.6427, 0.6310, 0.6196, 0.6084, 0.5978, 0.5877, 0.5782, 0.5695, 0.5615, 0.5543, 0.5479, 0.5424, 0.5376, 0.5337, 0.5306, 0.5283, 0.5268, 0.5260, 0.5260, 0.5267, 0.5281,
    0.7098, 0.7098, 0.7072, 0.7030, 0.6972, 0.6903, 0.6822, 0.6731, 0.6630, 0.6522, 0.6411, 0.6296, 0.6180, 0.6064, 0.5951, 0.5839, 0.5732, 0.5629, 0.5532, 0.5441, 0.5355, 0.5277, 0.5205, 0.5140, 0.5082, 0.5030, 0.4986, 0.4949, 0.4917, 0.4893, 0.4875, 0.4863,
    0.6917, 0.6916, 0.6893, 0.6853, 0.6799, 0.6735, 0.6658, 0.6571, 0.6475, 0.6371, 0.6263, 0.6150, 0.6035, 0.5918, 0.5802, 0.5687, 0.5575, 0.5465, 0.5359, 0.5258, 0.5161, 0.5069, 0.4983, 0.4903, 0.4828, 0.4758, 0.4695, 0.4637, 0.4585, 0.4538, 0.4497, 0.4461,
    0.6728, 0.6728, 0.6706, 0.6668, 0.6618, 0.6557, 0.6484, 0.6401, 0.6308, 0.6208, 0.6102, 0.5990, 0.5876, 0.5759, 0.5641, 0.5523, 0.5406, 0.5290, 0.5177, 0.5067, 0.4961, 0.4858, 0.4759, 0.4665, 0.4575, 0.4490, 0.4410, 0.4334, 0.4263, 0.4197, 0.4136, 0.4079,
    0.6534, 0.6533, 0.6512, 0.6477, 0.6430, 0.6371, 0.6302, 0.6221, 0.6131, 0.6034, 0.5929, 0.5820, 0.5705, 0.5588, 0.5469, 0.5348, 0.5227, 0.5107, 0.4988, 0.4870, 0.4755, 0.4643, 0.4534, 0.4428, 0.4325, 0.4227, 0.4132, 0.4042, 0.3955, 0.3872, 0.3793, 0.3718,
    0.6334, 0.6334, 0.6314, 0.6280, 0.6235, 0.6179, 0.6111, 0.6034, 0.5946, 0.5851, 0.5748, 0.5639, 0.5526, 0.5408, 0.5287, 0.5164, 0.5040, 0.4916, 0.4791, 0.4668, 0.4546, 0.4425, 0.4307, 0.4192, 0.4079, 0.3969, 0.3863, 0.3760, 0.3660, 0.3564, 0.3471, 0.3381,
    0.6131, 0.6131, 0.6111, 0.6079, 0.6035, 0.5981, 0.5915, 0.5839, 0.5754, 0.5660, 0.5559, 0.5450, 0.5337, 0.5219, 0.5097, 0.4973, 0.4846, 0.4718, 0.4590, 0.4461, 0.4333, 0.4207, 0.4081, 0.3958, 0.3837, 0.3719, 0.3603, 0.3490, 0.3380, 0.3273, 0.3169, 0.3069
};
static const float KC_EAVG[32] = {
    0.9990, 0.9990, 0.9960, 0.9911, 0.9844, 0.9761, 0.9662, 0.9549, 0.9420, 0.9278, 0.9122, 0.8951, 0.8767, 0.8569, 0.8358, 0.8134, 0.7897, 0.7649, 0.7391, 0.7123, 0.6848, 0.6566, 0.6280, 0.5991, 0.5701, 0.5411, 0.5123, 0.4839, 0.4560, 0.4287, 0.4023, 0.3766
};
// END kulla_conty_lut.py

static const uint KC_N = 32;

float ggxE(float mu, float roughness) {
    float x = sqrt(saturate(mu)) * (KC_N - 1);
    float y = saturate(roughness) * (KC_N - 1);
    uint x0 = min(uint(x), KC_N - 2), y0 = min(uint(y), KC_N - 2);
    float fx = x - x0, fy = y - y0;
    uint i = y0 * KC_N + x0;
    return lerp(lerp(KC_E[i], KC_E[i + 1], fx),
                lerp(KC_E[i + KC_N], KC_E[i + KC_N + 1], fx), fy);
}

// Roughness only: once per fragment, not per light.
float ggxEavg(float roughness) {
    float y = saturate(roughness) * (KC_N - 1);
    uint y0 = min(uint(y), KC_N - 2);
    return lerp(KC_EAVG[y0], KC_EAVG[y0 + 1], y - y0);
}

// Cook-Torrance GGX for one direct light: GGX normal distribution, Schlick-GGX
// geometry with the direct-lighting k, Schlick Fresnel. Returns radiance already
// multiplied by N.L, so the caller only scales by light color. eAvg is
// ggxEavg(roughness), hoisted out of the per-light loops.
float3 brdf(float3 n, float3 v, float3 l, float3 albedo, float metallic, float roughness,
            float eAvg) {
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

    // Kulla-Conty lobe: (1 - E(mu_o)) (1 - E(mu_i)) / (pi (1 - E_avg)) puts
    // back exactly the energy single scattering lost (white furnace at F = 1).
    // Fms colors it: each extra bounce picks up another average Fresnel F_avg
    // (Schlick's hemispherical average is f0 + (1 - f0) / 21).
    float fms = (1.0 - ggxE(ndotv, roughness)) * (1.0 - ggxE(ndotl, roughness)) /
                (PI * max(1.0 - eAvg, 1e-4));
    float3 fAvg = f0 + (1.0 - f0) / 21.0;
    float3 Fms = fAvg * fAvg * eAvg / (1.0 - fAvg * (1.0 - eAvg));
    spec += fms * Fms;
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
    float eAvg = ggxEavg(roughness);

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
        // Only the first directional light has a shadow map.
        float shadow = i == 0 ? sunShadow(worldPos) : 1.0;
        lit += shadow * ubo.dirLights[i].color.rgb *
               brdf(n, v, l, albedo, metallic, roughness, eAvg);
    }
    for (uint i = 0; i < ubo.counts.y; i++) {
        float3 d = ubo.pointLights[i].position.xyz - worldPos;
        float dist = length(d);
        float atten = windowedFalloff(dist, ubo.pointLights[i].position.w);
        if (atten <= 0.0) { continue; }
        lit += pointShadow(worldPos, i) *
               ubo.pointLights[i].color.rgb * ubo.pointLights[i].color.w * atten *
               brdf(n, v, d / max(dist, 1e-4), albedo, metallic, roughness, eAvg);
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
        lit += spotShadow(worldPos, i) *
               ubo.spotLights[i].color.rgb * ubo.spotLights[i].color.w *
               cone / (dist * dist + 1.0) *
               brdf(n, v, l, albedo, metallic, roughness, eAvg);
    }
    return float4(tonemapACES(lit), 1.0);
}
