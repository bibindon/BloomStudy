// bloom.fx

// === 入力テクスチャ ===
// 元のシーン
texture g_SceneTex;
sampler SceneSampler = sampler_state
{
    Texture = <g_SceneTex>;
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// 処理対象（BrightPassやBlurの入力として使う）
texture g_SrcTex;
sampler SrcSampler = sampler_state
{
    Texture = <g_SrcTex>;
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// ブラー済みテクスチャ（Combineで使用）
texture g_BlurTex;
sampler BlurSampler = sampler_state
{
    Texture = <g_BlurTex>;
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// === パラメータ ===
float g_Threshold = 0.6f; // 輝度しきい値
float2 g_TexelSize; // (1/width, 1/height) : ブラー用

// === BrightPass ===
float4 BrightPassPS(float2 texCoord : TEXCOORD0) : COLOR
{
    float4 c = tex2D(SrcSampler, texCoord);
    float lum = dot(c.rgb, float3(0.299, 0.587, 0.114));
    if (lum > g_Threshold)
        return c;
    return float4(0, 0, 0, 1);
}

float4 g_Direction;

// 追加パラメータ
float  g_BlurDecay   = 0.86;   // 0.82～0.90 で調整
float  g_BlurStep    = 1.5;    // サンプル間隔（縮小RTでのピクセル基準）
#define BLUR_SAMPLES 24        // 片側サンプル数（中心＋前後＝25tap）

// 置き換え: BlurPS（横/縦とも共通。g_Direction=(1,0) or (0,1)）
float4 BlurPS(float2 texCoord : TEXCOORD0) : COLOR
{
    float2 dir = normalize(g_Direction.xy);
    float2 oneStep = dir * g_TexelSize * g_BlurStep;

    float3 sum = tex2D(SrcSampler, texCoord).rgb;
    float  w   = 1.0f;
    float  wsum= 1.0f;

    float2 off = oneStep;

    [loop]
    for (int i = 1; i <= BLUR_SAMPLES; i++)
    {
        w *= g_BlurDecay;

        float3 fwd = tex2D(SrcSampler, texCoord + off).rgb;
        float3 bwd = tex2D(SrcSampler, texCoord - off).rgb;

        sum  += (fwd + bwd) * w;
        wsum += 2.0f * w;

        off += oneStep;
    }
    
    sum *= 3;

    return float4(sum / wsum, 1.0f);
}

// === Combine ===
// SceneTex + BlurTex を加算合成
float4 CombinePS(float2 texCoord : TEXCOORD0) : COLOR
{
    float4 scene = tex2D(SceneSampler, texCoord);
    float4 bloom = tex2D(BlurSampler, texCoord);
    
    bloom.r += (1.0 / 256.0) * 0.666;
    bloom.g += (1.0 / 256.0) * 0.333;
    bloom.b += (1.0 / 256.0) * 0.000;

    // ブルームの濃さ
    return scene + bloom * 1.0f;
}

// === Techniques ===
technique BrightPass
{
    pass P0
    {
        PixelShader = compile ps_3_0 BrightPassPS();
    }
}

technique Blur
{
    pass P0
    {
        PixelShader = compile ps_3_0 BlurPS();
    }
}

technique Combine
{
    pass P0
    {
        PixelShader = compile ps_3_0 CombinePS();
    }
}

