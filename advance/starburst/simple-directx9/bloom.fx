// bloom.fx  — Down/Up ピラミッド方式

// === 入力テクスチャ ===
texture g_SceneTex;   // 最終合成用（元シーン）
texture g_SrcTex;     // 現在の処理の入力
texture g_SrcTex2;    // Up 合成で使う“ひとつ上の解像度”のバッファ

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
float2 g_TexelSize;   // ★「ソース側」の 1/幅,1/高 をセットすること（パスごとに更新）
float  g_Threshold = 1.0f;  // 輝度しきい値（線形色）
float  g_Intensity = 1.0f;  // ブルーム強度（最終合成）

// ---------------- Bright Pass（明部抽出） ----------------
float3 LumaCoeffs = float3(0.299, 0.587, 0.114);
float4 PS_Bright(float2 uv:TEXCOORD0) : COLOR
{
    float3 c = tex2D(SrcS, uv).rgb;
    float  l = dot(c, LumaCoeffs);
    float  m = max(0, l - g_Threshold);     // ソフトニーを入れたければ smoothstep へ変更可
    float  k = m / max(l, 0.0001);
    return float4(c * saturate(k), 1);
}

// ------------- Downsample（2x縮小＋テントフィルタ）-------------
float4 PS_Down(float2 uv:TEXCOORD0) : COLOR
{
    // 入力（SrcS）のテクセルサイズ
    float2 s = g_TexelSize;

    // テント（中心=4, クロス=2, 隅=1）/16
    float4 c0 = tex2D(SrcS, uv);

    float4 cx = tex2D(SrcS, uv + float2(+s.x,    0)) +
                tex2D(SrcS, uv + float2(-s.x,    0)) +
                tex2D(SrcS, uv + float2(   0, +s.y)) +
                tex2D(SrcS, uv + float2(   0, -s.y));

    float4 cc = tex2D(SrcS, uv + s) +
                tex2D(SrcS, uv + float2(+s.x, -s.y)) +
                tex2D(SrcS, uv + float2(-s.x, +s.y)) +
                tex2D(SrcS, uv - s);

     return (c0 * 4.0 + cx * 2.0 + cc) / 16.0;
    //return (c0 + cx + cc) / 9.0;
}

float g_LevelGain = 1.0;   // ← このパス（このレベル）専用の重み

// ------------- Upsample（拡大＋Add 合成）-------------
float4 PS_UpsampleAdd(float2 uv:TEXCOORD0) : COLOR
{
    // 低解像度のブルーム（SrcS）をテントで少し広げて拡大
    float2 s = g_TexelSize;

    float4 low  = ( tex2D(SrcS, uv) * 4.0 +
                    tex2D(SrcS, uv + float2(+s.x, 0)) +
                    tex2D(SrcS, uv + float2(-s.x, 0)) +
                    tex2D(SrcS, uv + float2(0, +s.y)) +
                    tex2D(SrcS, uv + float2(0, -s.y)) +
                    tex2D(SrcS, uv + s) +
                    tex2D(SrcS, uv + float2(+s.x, -s.y)) +
                    tex2D(SrcS, uv + float2(-s.x, +s.y)) +
                    tex2D(SrcS, uv - s) ) / 12.0;

    float4 low2  = (tex2D(SrcS, uv) +
                    tex2D(SrcS, uv + float2(+s.x, 0)) +
                    tex2D(SrcS, uv + float2(-s.x, 0)) +
                    tex2D(SrcS, uv + float2(0, +s.y)) +
                    tex2D(SrcS, uv + float2(0, -s.y)) +
                    tex2D(SrcS, uv + s) +
                    tex2D(SrcS, uv + float2(+s.x, -s.y)) +
                    tex2D(SrcS, uv + float2(-s.x, +s.y)) +
                    tex2D(SrcS, uv - s) ) / 9.0;

    // ひとつ上のレベル（SrcS2）を加算
    float4 hi = tex2D(SrcS2, uv);
    return low * g_LevelGain + hi;
    //return low2 * g_LevelGain + hi;
    //return low2 + hi;
}

// -------------------- 最終合成 --------------------
float4 PS_Combine(float2 uv:TEXCOORD0) : COLOR
{
    float3 scene = tex2D(SceneS, uv).rgb;
    float3 bloom = tex2D(SrcS,   uv).rgb;   // up 最上位（フル解像度）の結果をバインド
    return float4(scene + bloom * g_Intensity, 1.0);
}

technique BrightPass { pass P0 { PixelShader = compile ps_3_0 PS_Bright(); } }
technique Down       { pass P0 { PixelShader = compile ps_3_0 PS_Down();   } }
technique Upsample   { pass P0 { PixelShader = compile ps_3_0 PS_UpsampleAdd(); } }
technique Combine    { pass P0 { PixelShader = compile ps_3_0 PS_Combine(); } }

// ==== 追加パラメータ ====
int   g_StreakCount     = 8;     // アーム本数（4/6/8 など）
int   g_StreakSteps     = 8;     // 1 本あたりのサンプル数（<=16 目安）
float g_StreakLengthPx  = 60.0;  // このレベルの「ピクセル」単位の長さ
float g_StreakAtten     = 0.85;  // 距離減衰（0.7〜0.95）
float g_StreakAngleRad  = 0.0;   // 回転（ラジアン）
float g_StreakIntensity = 5.0;   // 強さ

// ==== 追加ピクセルシェーダ ====
float4 PS_Starburst(float2 uv:TEXCOORD0) : COLOR
{
    float3 src = tex2D(SrcS, uv).rgb;

    float2 texel = g_TexelSize;
    float  denom = (float)(g_StreakCount * g_StreakSteps) * 2.0; // ±両方向

    float3 acc = 0;

    // 方向を等間隔に生成（[0, PI) に g_StreakCount 本）
    [loop] for (int k = 0; k < 8; ++k)   // 上限8本
    {
        if (k >= g_StreakCount) break;

        float ang = g_StreakAngleRad + (3.14159265 / (float)g_StreakCount) * k;
        float2 dir = float2(cos(ang), sin(ang));

        // 1本あたりの積分（±両方向）
        [loop] for (int s = 1; s <= 16; ++s) // 上限16ステップ
        {
            if (s > g_StreakSteps) break;

            float t = (float)s / (float)g_StreakSteps;
            float2 offs = dir * (g_StreakLengthPx * t) * texel;
            float  w    = pow(g_StreakAtten, (float)s);

            acc += tex2Dlod(SrcS, float4(uv + offs, 0, 0)).rgb * w;
            acc += tex2Dlod(SrcS, float4(uv - offs, 0, 0)).rgb * w; // 反対側も足す
        }
    }

    float3 star = acc * (g_StreakIntensity / max(denom, 1.0));
    return float4(src + star, 1);  // 元の明部にスターを加算
}

technique Starburst { pass P0 { PixelShader = compile ps_3_0 PS_Starburst(); } }

