// bloom.fx — Starburst only (0°, 45°, 135°), no bloom
// 入力：フル解像度のシーンと、スターバースト用の小RT
// 手順：BrightDown(シーン→小RT) → StreakDirectional(小RT→小RT蓄積) → Combine(シーン+Streak)

texture g_SceneTex;     // フル解像度シーン
texture g_SrcTex;       // 現在の処理入力（BrightDown / Streak で使用）
texture g_StreakTex;    // スターバースト蓄積（小RT）

sampler SceneS = sampler_state
{
    Texture   = <g_SceneTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU  = CLAMP;
    AddressV  = CLAMP;
};

sampler SrcS = sampler_state
{
    Texture   = <g_SrcTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU  = CLAMP;
    AddressV  = CLAMP;
};

sampler StreakS = sampler_state
{
    Texture   = <g_StreakTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU  = CLAMP;
    AddressV  = CLAMP;
};

// パラメータ
float2 g_TexelSize;        // 入力テクスチャ 1texel（BrightDown時はシーン、Streak時は小RT）
float  g_Threshold;        // 明部抽出しきい値

float2 g_Direction;        // Streak 方向ベクトル（正規化して使用）
float  g_StreakDecay;      // 減衰（0.80～0.92 目安）
float  g_StreakStep;       // サンプル間隔（1.0～2.0 目安）
float  g_StreakIntensity;  // 最終合成での強さ
float  g_StreakGain;       // 方向ブラー内部ゲイン

// 明部抽出＋縮小（テント近似）
// 入力はフル解像度のシーン、出力は小RT
float4 PS_BrightDown(float2 uv : TEXCOORD0) : COLOR
{
    float2 s = g_TexelSize;

    float3 c0 = tex2D(SrcS, uv).rgb;

    float3 cx =
          tex2D(SrcS, uv + float2(+s.x, 0)).rgb
        + tex2D(SrcS, uv + float2(-s.x, 0)).rgb
        + tex2D(SrcS, uv + float2(0, +s.y)).rgb
        + tex2D(SrcS, uv + float2(0, -s.y)).rgb;

    float3 cd =
          tex2D(SrcS, uv + s).rgb
        + tex2D(SrcS, uv + float2(+s.x, -s.y)).rgb
        + tex2D(SrcS, uv + float2(-s.x, +s.y)).rgb
        + tex2D(SrcS, uv - s).rgb;

    float3 avg = (c0 * 4.0 + cx * 2.0 + cd) / 16.0;

    float luminance = dot(avg, float3(0.299, 0.587, 0.114));
    float mask      = max(0.0, luminance - g_Threshold);

    return float4(mask, mask, mask, 1.0);
}

// スターバースト（指定方向の対称 1D ブラー、指数減衰）
#define STREAK_SAMPLES 12

float4 PS_StreakDirectional(float2 uv : TEXCOORD0) : COLOR
{
    float2 directionNormalized = normalize(g_Direction);
    float2 oneStep              = directionNormalized * g_TexelSize * g_StreakStep;

    float3 sumColor  = tex2D(SrcS, uv).rgb;
    float  weight    = 1.0;
    float  weightSum = 1.0;
    float2 offsetUv  = oneStep;

    [loop]
    for (int i = 1; i <= STREAK_SAMPLES; ++i)
    {
        weight *= g_StreakDecay;

        float3 forwardSample  = tex2D(SrcS, uv + offsetUv).rgb;
        float3 backwardSample = tex2D(SrcS, uv - offsetUv).rgb;

        sumColor  += (forwardSample + backwardSample) * weight;
        weightSum += 2.0 * weight;

        offsetUv += oneStep;
    }

    float3 result = (sumColor / weightSum) * g_StreakGain;

    return float4(result, 1.0);
}

// 最終合成（Scene + Streak）
float4 PS_Combine(float2 uv : TEXCOORD0) : COLOR
{
    float3 sceneColor  = tex2D(SceneS,  uv).rgb;
    float3 streakColor = tex2D(StreakS, uv).rgb;

    float3 outColor = sceneColor + streakColor * g_StreakIntensity;

    return float4(outColor, 1.0);
}

technique BrightDown
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_BrightDown();
    }
}

technique StreakDirectional
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_StreakDirectional();
    }
}

technique Combine
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_Combine();
    }
}
