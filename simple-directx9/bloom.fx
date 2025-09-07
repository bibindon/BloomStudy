// bloom.fx

// === ���̓e�N�X�`�� ===
// ���̃V�[��
texture g_SceneTex;
sampler SceneSampler = sampler_state
{
    Texture = <g_SceneTex>;
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// �����ΏہiBrightPass��Blur�̓��͂Ƃ��Ďg���j
texture g_SrcTex;
sampler SrcSampler = sampler_state
{
    Texture = <g_SrcTex>;
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// �u���[�ς݃e�N�X�`���iCombine�Ŏg�p�j
texture g_BlurTex;
sampler BlurSampler = sampler_state
{
    Texture = <g_BlurTex>;
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// === �p�����[�^ ===
float g_Threshold = 0.3f; // �P�x�������l
float2 g_TexelSize; // (1/width, 1/height) : �u���[�p

// === BrightPass ===
float4 BrightPassPS(float2 texCoord : TEXCOORD0) : COLOR
{
    float4 c = tex2D(SrcSampler, texCoord);
    float lum = dot(c.rgb, float3(0.299, 0.587, 0.114));
    if (lum > g_Threshold)
        return c;
    return float4(0, 0, 0, 1);
}

// ���a�Œ�i7 �� 15tap�j
static const int RADIUS = 51; // �
static const float SIGMA = 40.0f;

float4 BlurPS(float2 texCoord : TEXCOORD0) : COLOR
{
    float2 step = float2(g_TexelSize.x, 0);
    float4 sum = 0;
    float weightSum = 0;

    [unroll]
    for (int i = -RADIUS; i <= RADIUS; i++)
    {
        float w = exp2(-(i * i) / (2.0 * SIGMA * SIGMA) * 1.442695f); // exp(x) = exp2(x * log2(e))
        sum += tex2D(SrcSampler, texCoord + step * i) * w;
        weightSum += w;
    }
    return sum / weightSum;
}

// === Combine ===
// SceneTex + BlurTex �����Z����
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

