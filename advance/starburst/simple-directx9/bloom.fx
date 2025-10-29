// bloom.fx  — Bloom + Starburst (0°, 45°, 135°) 低解像度演算

// === 入力テクスチャ ===

// 最終合成用（元シーン）
texture g_SceneTex;

// 現在の処理の入力
texture g_SrcTex;

// Up 合成で使う“ひとつ上の解像度”のバッファ
texture g_SrcTex2;

// スターバースト蓄積（低解像度で生成したものをそのまま合成時に拡大サンプル）
texture g_StreakTex;

sampler SceneS = sampler_state
{
    Texture  = <g_SceneTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

sampler SrcS = sampler_state
{
    Texture  = <g_SrcTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

sampler SrcS2 = sampler_state
{
    Texture  = <g_SrcTex2>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

sampler StreakS = sampler_state
{
    Texture  = <g_StreakTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

// === パラメータ ===

float2 g_TexelSize;

float g_Threshold;

float g_Intensity;

// --- スターバースト用 ---
float2 g_Direction;
float  g_StreakDecay;       // 0.80～0.92 あたり
float  g_StreakStep;        // 1.0～2.0 ピクセル相当
float  g_StreakIntensity;   // 最終合成時の強さ
float  g_StreakGain;        // 方向ブラー内部ゲイン

// ---------------- Bright Pass（明部抽出） ----------------

float4 PS_Bright(float2 inUv : TEXCOORD0) : COLOR
{
    float3 srcRgb = tex2D(SrcS, inUv).rgb;

    float luminous =
        srcRgb.r * 0.2 +
        srcRgb.g * 0.7 +
        srcRgb.b * 0.1;

    if (luminous < g_Threshold)
    {
        luminous = 0.0;
    }

    return float4(luminous, luminous, luminous, 1.0);
}

// ------------- Downsample（2x縮小＋テントフィルタ）-------------
float4 PS_Down(float2 inUv : TEXCOORD0) : COLOR
{
    float2 stepUv = g_TexelSize;

    float4 center = tex2D(SrcS, inUv);

    float4 crossSum =
          tex2D(SrcS, inUv + float2(+stepUv.x, 0))
        + tex2D(SrcS, inUv + float2(-stepUv.x, 0))
        + tex2D(SrcS, inUv + float2(0, +stepUv.y))
        + tex2D(SrcS, inUv + float2(0, -stepUv.y));

    float4 diagSum =
          tex2D(SrcS, inUv + stepUv)
        + tex2D(SrcS, inUv + float2(+stepUv.x, -stepUv.y))
        + tex2D(SrcS, inUv + float2(-stepUv.x, +stepUv.y))
        + tex2D(SrcS, inUv - stepUv);

    return (center * 4.0 + crossSum * 2.0 + diagSum) / 16.0;
}

// ------------- Upsample（拡大＋Add 合成）-------------
float4 PS_UpsampleAdd(float2 inUv : TEXCOORD0) : COLOR
{
    float2 stepUv = g_TexelSize;

    float4 center = tex2D(SrcS, inUv) * 4.0;

    float4 crossSum = 0.0;
    crossSum += tex2D(SrcS, inUv + float2(+stepUv.x, 0));
    crossSum += tex2D(SrcS, inUv + float2(-stepUv.x, 0));
    crossSum += tex2D(SrcS, inUv + float2(0, +stepUv.y));
    crossSum += tex2D(SrcS, inUv + float2(0, -stepUv.y));

    float4 diagSum = 0.0;
    diagSum += tex2D(SrcS, inUv + stepUv) * 2.0;
    diagSum += tex2D(SrcS, inUv + float2(+stepUv.x, -stepUv.y)) * 2.0;
    diagSum += tex2D(SrcS, inUv + float2(-stepUv.x, +stepUv.y)) * 2.0;
    diagSum += tex2D(SrcS, inUv - stepUv) * 2.0;

    float4 low = (center + crossSum + diagSum) / 16.0;

    float4 hi = tex2D(SrcS2, inUv);

    return low + hi;
}

// -------- スターバースト（指定方向の多タップ・ブラー）--------
#define STREAK_SAMPLES 12

float4 PS_StreakDirectional(float2 inUv : TEXCOORD0) : COLOR
{
    float2 dirNorm = normalize(g_Direction);

    float2 oneStep = dirNorm * g_TexelSize * g_StreakStep;

    float3 sumColor = tex2D(SrcS, inUv).rgb;

    float  decayWeight = 1.0;
    float  weightAccum = 1.0;

    float2 offsetUv = oneStep;

    [loop]
    for (int i = 1; i <= STREAK_SAMPLES; ++i)
    {
        decayWeight *= g_StreakDecay;

        float3 forwardSample = tex2D(SrcS, inUv + offsetUv).rgb;
        float3 backwardSample = tex2D(SrcS, inUv - offsetUv).rgb;

        sumColor += (forwardSample + backwardSample) * decayWeight;

        weightAccum += 2.0 * decayWeight;

        offsetUv += oneStep;
    }

    float3 result = (sumColor / weightAccum) * g_StreakGain;

    return float4(result, 1.0);
}

// -------------------- 最終合成 --------------------
float4 PS_Combine(float2 inUv : TEXCOORD0) : COLOR
{
    float3 sceneRgb  = tex2D(SceneS,  inUv).rgb;

    float3 bloomRgb  = tex2D(SrcS,    inUv).rgb;

    float3 streakRgb = tex2D(StreakS, inUv).rgb;

    if (true)
    {
        bloomRgb.r += 0.666 / 256;
        bloomRgb.g += 0.333 / 256;
        bloomRgb.b += 0.000 / 256;
    }

    float3 outRgb = sceneRgb
                  + bloomRgb * g_Intensity
                  + streakRgb * g_StreakIntensity;

    return float4(outRgb, 1.0);
}

technique BrightPass { pass P0 { PixelShader = compile ps_3_0 PS_Bright(); } }
technique Down       { pass P0 { PixelShader = compile ps_3_0 PS_Down();   } }
technique Upsample   { pass P0 { PixelShader = compile ps_3_0 PS_UpsampleAdd(); } }

// 新規：低解像度の方向ブラー
technique StreakDirectional { pass P0 { PixelShader = compile ps_3_0 PS_StreakDirectional(); } }

technique Combine    { pass P0 { PixelShader = compile ps_3_0 PS_Combine(); } }
