// bloom.fx

// === 入力テクスチャ ===

// 最終合成用（元シーン）
texture g_SceneTex;

// 現在の処理の入力
texture g_SrcTex;

// Up 合成で使う“ひとつ上の解像度”のバッファ
texture g_SrcTex2;

sampler SceneS = sampler_state
{
    Texture = <g_SceneTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

sampler SrcS = sampler_state
{
    Texture = <g_SrcTex>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

sampler SrcS2 = sampler_state
{
    Texture = <g_SrcTex2>;
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

// ---------------- Bright Pass（明部抽出） ----------------

float4 PS_Bright(float2 uv:TEXCOORD0) : COLOR
{
    float3 color = tex2D(SrcS, uv).rgb;
    float luminous = color.r * 0.2 + color.g * 0.7 + color.b * 0.1;
    if (luminous < g_Threshold)
    {
        luminous = 0.f;
    }

    return float4(luminous, luminous, luminous, 1.0);
}

// ------------- Downsample（2x縮小＋テントフィルタ）-------------
float4 PS_Down(float2 uv : TEXCOORD0) : COLOR
{
    float2 s = g_TexelSize;

    float4 c0 = tex2D(SrcS, uv);

    float4 cx = tex2D(SrcS, uv + float2(+s.x,    0)) +
                tex2D(SrcS, uv + float2(-s.x,    0)) +
                tex2D(SrcS, uv + float2(   0, +s.y)) +
                tex2D(SrcS, uv + float2(   0, -s.y));

    float4 cc = tex2D(SrcS, uv + s) +
                tex2D(SrcS, uv + float2(+s.x, -s.y)) +
                tex2D(SrcS, uv + float2(-s.x, +s.y)) +
                tex2D(SrcS, uv - s);

    return (c0 + cx + cc) / 9.0;
}

float4 PS_UpsampleAdd(float2 uv : TEXCOORD0) : COLOR
{
    float2 texelSize = g_TexelSize;

    // 1) 中心
    float4 sumCenter = tex2D(SrcS, uv);

    // 2) 上下左右
    float4 sumCross = 0.0;
    sumCross += tex2D(SrcS, uv + float2(+texelSize.x, 0.0));
    sumCross += tex2D(SrcS, uv + float2(-texelSize.x, 0.0));
    sumCross += tex2D(SrcS, uv + float2(0.0, +texelSize.y));
    sumCross += tex2D(SrcS, uv + float2(0.0, -texelSize.y));

    // 3) 斜め
    float4 sumDiag = 0.0;
    sumDiag += tex2D(SrcS, uv + texelSize);
    sumDiag += tex2D(SrcS, uv + float2(+texelSize.x, -texelSize.y));
    sumDiag += tex2D(SrcS, uv + float2(-texelSize.x, +texelSize.y));
    sumDiag += tex2D(SrcS, uv - texelSize);

    float4 low = (sumCenter + sumCross + sumDiag) / 9.0;

    // ひとつ上のレベル（SrcS2）を加算
    float4 hi = tex2D(SrcS2, uv);
    return low + hi;
}

float4 PS_Combine(float2 uv:TEXCOORD0) : COLOR
{
    float3 scene = tex2D(SceneS, uv).rgb;
    float3 bloom = tex2D(SrcS,   uv).rgb;

    // 0.5倍してから0.5乗すると柔らかくて広い光になる
    bloom *= 0.5;
    bloom = pow(bloom, 0.5);

    // 256段階ではなく768段階の輝度にしたい場合
    if (true)
    {
        bloom.r += 0.666 / 256;
        bloom.g += 0.333 / 256;
        bloom.b += 0.000 / 256;
    }

    return float4(scene + bloom * g_Intensity, 1.0);
}

technique BrightPass
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_Bright();
    }
}

technique Down
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_Down();
    }
}

technique Upsample
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_UpsampleAdd();
    }
}

technique Combine
{
    pass P0
    {
        PixelShader = compile ps_3_0 PS_Combine();
    }
}

