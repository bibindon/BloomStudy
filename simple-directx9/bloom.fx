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
float g_Threshold = 0.1f; // 輝度しきい値
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

float4 BlurPS(float2 texCoord : TEXCOORD0) : COLOR
{
    float4 sum = float4(0, 0, 0, 0);
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(-6, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(-5, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(-4, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(-3, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(-2, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(-1, 0));
    sum += tex2D(SrcSampler, texCoord);
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(1, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(2, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(3, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(4, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(5, 0));
    sum += tex2D(SrcSampler, texCoord + g_TexelSize * float2(6, 0));
    return sum / 13.0f;
}

// === Combine ===
// SceneTex + BlurTex を加算合成
float4 CombinePS(float2 texCoord : TEXCOORD0) : COLOR
{
    float4 scene = tex2D(SceneSampler, texCoord);
    float4 bloom = tex2D(BlurSampler, texCoord);
    return scene + bloom * 0.7f;
}

// === Techniques ===
technique BrightPass
{
    pass P0
    {
        PixelShader = compile ps_2_0 BrightPassPS();
    }
}

technique Blur
{
    pass P0
    {
        PixelShader = compile ps_2_0 BlurPS();
    }
}

technique Combine
{
    pass P0
    {
        PixelShader = compile ps_2_0 CombinePS();
    }
}

