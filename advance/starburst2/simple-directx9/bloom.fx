// bloom.fx  — starburst only

// === 入力テクスチャ ===
texture g_SceneTex;   // 最終合成用（元シーン）
texture g_SrcTex;     // 現在の処理の入力
texture g_SrcTex2;    // 合成や加算に使う追加入力（Upの一つ上、または加算用）

sampler SceneS = sampler_state
{
    Texture = <g_SceneTex>;
    MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};

sampler SrcS = sampler_state
{
    Texture = <g_SrcTex>;
    MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};

sampler SrcS2 = sampler_state
{
    Texture = <g_SrcTex2>;
    MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};

// === パラメータ ===
float2 g_TexelSize;   // (1/w, 1/h)
float  g_Threshold;   // 明部抽出しきい値
float  g_Intensity;   // 最終合成時の強さ

// ---------------- Bright Pass（明部抽出） ----------------
float4 PS_Bright(float2 uv : TEXCOORD0) : COLOR
{
    float3 color    = tex2D(SrcS, uv).rgb;
    float  luminous = color.r * 0.2 + color.g * 0.7 + color.b * 0.1;
    if (luminous < g_Threshold)
    {
        luminous = 0.0;
    }
    return float4(luminous, luminous, luminous, 1.0);
}

// ============= 方向ペア 3x3（中心＋2ピクセル） =============
float4 PairH(float2 uv)     // 0°（左右）
{
    float2 s = g_TexelSize;
    float4 c = tex2D(SrcS, uv);
    c += tex2D(SrcS, uv + float2(+s.x, 0));
    c += tex2D(SrcS, uv + float2(-s.x, 0));
    return c / 3.0;
}

float4 PairD45(float2 uv)   // 45°（左上＋右下）
{
    float2 s = g_TexelSize;
    float4 c = tex2D(SrcS, uv);
    c += tex2D(SrcS, uv + float2(-s.x, +s.y));
    c += tex2D(SrcS, uv + float2(+s.x, -s.y));
    return c / 3.0;
}

float4 PairD135(float2 uv)  // 135°（右上＋左下）
{
    float2 s = g_TexelSize;
    float4 c = tex2D(SrcS, uv);
    c += tex2D(SrcS, uv + float2(+s.x, +s.y));
    c += tex2D(SrcS, uv + float2(-s.x, -s.y));
    return c / 3.0;
}

// ------------- Downsample（2x縮小相当） 方向版 -------------
float4 PS_Down_H  (float2 uv : TEXCOORD0) : COLOR { return PairH(uv);     }
float4 PS_Down_D45(float2 uv : TEXCOORD0) : COLOR { return PairD45(uv);   }
float4 PS_Down_D135(float2 uv : TEXCOORD0) : COLOR{ return PairD135(uv);  }

// ------------- Upsample ＋ “上の段”を加算（方向版） -------------
float4 PS_UpsampleAdd_H(float2 uv : TEXCOORD0) : COLOR
{
    float4 low = PairH(uv);
    float4 hi  = tex2D(SrcS2, uv);   // ひとつ上のレベル
    return low + hi;
}

float4 PS_UpsampleAdd_D45(float2 uv : TEXCOORD0) : COLOR
{
    float4 low = PairD45(uv);
    float4 hi  = tex2D(SrcS2, uv);
    return low + hi;
}

float4 PS_UpsampleAdd_D135(float2 uv : TEXCOORD0) : COLOR
{
    float4 low = PairD135(uv);
    float4 hi  = tex2D(SrcS2, uv);
    return low + hi;
}

// ------------- 汎用：コピー／2枚加算／最終合成 -------------
float4 PS_Copy(float2 uv : TEXCOORD0) : COLOR
{
    return tex2D(SrcS, uv);
}

float4 PS_Add2(float2 uv : TEXCOORD0) : COLOR
{
    return tex2D(SrcS, uv) + tex2D(SrcS2, uv);
}

float4 PS_CombineStar(float2 uv : TEXCOORD0) : COLOR
{
    float3 scene = tex2D(SceneS, uv).rgb;
    float3 star  = tex2D(SrcS,   uv).rgb;
    
    star *= 0.5;
    star = pow(star, 0.5);
    
    return float4(scene + star * g_Intensity, 1.0);
}

// ------------------------- Techniques -------------------------
technique BrightPass
{
    pass P0 { PixelShader = compile ps_3_0 PS_Bright(); }
}

technique Down_H     { pass P0 { PixelShader = compile ps_3_0 PS_Down_H();     } }
technique Down_D45   { pass P0 { PixelShader = compile ps_3_0 PS_Down_D45();   } }
technique Down_D135  { pass P0 { PixelShader = compile ps_3_0 PS_Down_D135();  } }

technique Up_H       { pass P0 { PixelShader = compile ps_3_0 PS_UpsampleAdd_H();     } }
technique Up_D45     { pass P0 { PixelShader = compile ps_3_0 PS_UpsampleAdd_D45();   } }
technique Up_D135    { pass P0 { PixelShader = compile ps_3_0 PS_UpsampleAdd_D135();  } }

technique Copy       { pass P0 { PixelShader = compile ps_3_0 PS_Copy();   } }
technique Add2       { pass P0 { PixelShader = compile ps_3_0 PS_Add2();   } }
technique CombineStar{ pass P0 { PixelShader = compile ps_3_0 PS_CombineStar(); } }
