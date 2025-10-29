#pragma comment( lib, "d3d9.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif

#include <d3d9.h>
#include <d3dx9.h>
#include <string>
#include <tchar.h>
#include <cassert>
#include <crtdbg.h>
#include <vector>

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
LPD3DXMESH g_pMesh = NULL;
std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_pEffect = NULL;        // simple.fx
LPD3DXEFFECT g_pBloomEffect = NULL;   // bloom.fx
bool g_bClose = false;  

// --- Bloom 用テクスチャ（※サーフェイスは都度ローカル取得） ---
LPDIRECT3DTEXTURE9 g_pSceneTex = NULL;
LPDIRECT3DTEXTURE9 g_pBrightTex = NULL;
LPDIRECT3DTEXTURE9 g_pBlurTexH = NULL;
LPDIRECT3DTEXTURE9 g_pBlurTexV = NULL;

// 1/16あたりから始めれば問題ない。そんなような気がしたが勘違いだったようだ
static const int kLevels = 8; // 1/2, 1/4, 1/8, 1/16, 1/32, 1/64
static const int kStartIndex = 0;
static const int kLevelRange = kLevels - kStartIndex;
LPDIRECT3DTEXTURE9 g_texDown[kLevelRange] = {0};
LPDIRECT3DTEXTURE9 g_texUp[kLevelRange]   = {0};

float g_fThreshold = 0.7f;
float g_fIntensity = 0.0f;
float g_fTime = 0.0f;

// 低解像度スターバーストで使うレベル
// i=0 が 1/2, i=1 が 1/4, i=2 が 1/8, i=3 が 1/16
static const int kStreakLevel = 4;

// スターバースト用 低解像度の蓄積テクスチャ
LPDIRECT3DTEXTURE9 g_pStreakTex = NULL;

// スターバースト用パラメータ
float g_fStreakIntensity = 0.8f;
float g_fStreakDecay     = 0.86f;
float g_fStreakStep      = 1.0f;
float g_fStreakGain      = 1.0f;

struct SCREENVERTEX
{
    float x, y, z, rhw;
    float u, v;
};

static void SetTexelSizeFromTexture(LPDIRECT3DTEXTURE9 tex)
{
    D3DSURFACE_DESC surfaceDesc;
    tex->GetLevelDesc(0, &surfaceDesc);

    D3DXVECTOR4 texelSize = D3DXVECTOR4(0, 0, 0, 0);

    texelSize.x = 1.0f / surfaceDesc.Width;
    texelSize.y = 1.0f / surfaceDesc.Height;
    texelSize.z = 0.f;
    texelSize.w = 0.f;

    g_pBloomEffect->SetVector("g_TexelSize", &texelSize);
}

static void InitD3D(HWND hWnd);
static void Cleanup();
static void Render();
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPTSTR lpCmdLine,
                            _In_ int nCmdShow);

int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    WNDCLASSEX wc { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = _T("Window1");
    wc.hIconSm = NULL;

    ATOM atom = RegisterClassEx(&wc);
    assert(atom != 0);

    RECT rect;
    SetRect(&rect, 0, 0, 1600, 900);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0;
    rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Starbusrt Sample"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right, rect.bottom,
                             NULL, NULL, wc.hInstance, NULL);

    InitD3D(hWnd);
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    MSG msg;
    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
        }

        Render();

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();
    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void InitD3D(HWND hWnd)
{
    HRESULT hResult = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp; ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;

    hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &d3dpp, &g_pd3dDevice);
    if (FAILED(hResult))
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                       &d3dpp, &g_pd3dDevice);
        assert(hResult == S_OK);
    }

    // メッシュ読み込み
    LPD3DXBUFFER pD3DXMtrlBuffer = NULL;
    hResult = D3DXLoadMeshFromX(_T("cube.x"), D3DXMESH_SYSTEMMEM,
                                g_pd3dDevice, NULL, &pD3DXMtrlBuffer,
                                NULL, &g_dwNumMaterials, &g_pMesh);
    assert(hResult == S_OK);

    D3DXMATERIAL* d3dxMaterials = (D3DXMATERIAL*)pD3DXMtrlBuffer->GetBufferPointer();
    g_pMaterials.resize(g_dwNumMaterials);
    g_pTextures.resize(g_dwNumMaterials);

    for (DWORD i = 0; i < g_dwNumMaterials; i++)
    {
        g_pMaterials[i] = d3dxMaterials[i].MatD3D;
        g_pMaterials[i].Ambient = g_pMaterials[i].Diffuse;
        g_pTextures[i] = NULL;

        std::string pTexPath(d3dxMaterials[i].pTextureFilename);
        if (!pTexPath.empty())
        {
            hResult = D3DXCreateTextureFromFileA(g_pd3dDevice, pTexPath.c_str(), &g_pTextures[i]);
            assert(hResult == S_OK);
        }
    }
    pD3DXMtrlBuffer->Release();

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_pEffect,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("bloom.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_pBloomEffect,
                                       NULL);
    assert(hResult == S_OK);

    // 各テクスチャ作成（サーフェイスは保持しない）
    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                      D3DPOOL_DEFAULT, &g_pSceneTex);

    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                      D3DPOOL_DEFAULT, &g_pBrightTex);

    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                      D3DPOOL_DEFAULT, &g_pBlurTexH);

    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                      D3DPOOL_DEFAULT, &g_pBlurTexV);

    int w = 1600;
    int h = 900;

    for (int i = 0; i < kLevels; ++i)
    {
        w = (std::max)(1, w / 2);
        h = (std::max)(1, h / 2);

        if (i < kStartIndex)
        {
            continue;
        }

        int index2 = i - kStartIndex;

        D3DXCreateTexture(g_pd3dDevice, w, h, 1,
                          D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                          D3DPOOL_DEFAULT, &g_texDown[index2]);

        D3DXCreateTexture(g_pd3dDevice, w, h, 1,
                          D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                          D3DPOOL_DEFAULT, &g_texUp[index2]);

        if (i == kStreakLevel)
        {
            D3DXCreateTexture(g_pd3dDevice, w, h, 1,
                              D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                              D3DPOOL_DEFAULT, &g_pStreakTex);
        }
    }
}

void Cleanup()
{
    for (auto& texture : g_pTextures)
    {
        SAFE_RELEASE(texture);
    }

    for (int i = 0; i < kLevelRange; ++i)
    {
        SAFE_RELEASE(g_texDown[i]);
        SAFE_RELEASE(g_texUp[i]);
    }

    SAFE_RELEASE(g_pMesh);
    SAFE_RELEASE(g_pEffect);
    SAFE_RELEASE(g_pBloomEffect);

    SAFE_RELEASE(g_pSceneTex);
    SAFE_RELEASE(g_pBrightTex);
    SAFE_RELEASE(g_pBlurTexH);
    SAFE_RELEASE(g_pBlurTexV);

    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

static void DrawFullScreenQuadCurrentRT(LPD3DXEFFECT fx)
{
    LPDIRECT3DSURFACE9 renderTarget = NULL;
    g_pd3dDevice->GetRenderTarget(0, &renderTarget);
    D3DSURFACE_DESC surfaceDesc = {};
    renderTarget->GetDesc(&surfaceDesc);
    renderTarget->Release();

    const float w = (float)surfaceDesc.Width;
    const float h = (float)surfaceDesc.Height;

    SCREENVERTEX v[4] =
    {
        { -0.5f,   -0.5f, 0, 1, 0, 0 },
        { w-0.5f,  -0.5f, 0, 1, 1, 0 },
        { -0.5f,  h-0.5f, 0, 1, 0, 1 },
        { w-0.5f, h-0.5f, 0, 1, 1, 1 },
    };

    g_pd3dDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

    UINT nPassNum = 0;
    fx->Begin(&nPassNum, 0);
    fx->BeginPass(0);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(SCREENVERTEX));
    fx->EndPass();
    fx->End();
}

static void Render()
{
    //==============================================================
    // 0) シーンを g_pSceneTex に描く（simple.fx）
    //==============================================================
    LPDIRECT3DSURFACE9 oldRT = NULL;
    g_pd3dDevice->GetRenderTarget(0, &oldRT);

    LPDIRECT3DSURFACE9 sceneRT = NULL;
    g_pSceneTex->GetSurfaceLevel(0, &sceneRT);
    g_pd3dDevice->SetRenderTarget(0, sceneRT);

    // クリア（黒 + Z）
    g_pd3dDevice->Clear(0,
                        NULL,
                        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                        D3DCOLOR_XRGB(0, 0, 0),
                        1.0f,
                        0);

    // 行列セット
    D3DXMATRIX W, V, P, WVP;

    g_fTime += 0.005f;

    D3DXMatrixRotationY(&W, g_fTime);
    D3DXVECTOR3 eye(0, 2.2f, -5.0f);
    D3DXVECTOR3 at(0, 0.8f, 0);
    D3DXVECTOR3 up(0, 1, 0);
    D3DXMatrixLookAtLH(&V, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&P, D3DXToRadian(60.f), 1600.0f/900.0f, 0.1f, 100.0f);
    WVP = W * V * P;

    // simple.fx（Technique1）でメッシュ描画
    g_pEffect->SetTechnique("Technique1");
    g_pEffect->SetMatrix("g_matWorldViewProj", &WVP);
    D3DXVECTOR4 lightDir(1.0f, 1.0f, 1.0f, 1.0f);
    D3DXVec4Normalize(&lightDir, &lightDir);
    g_pEffect->SetVector("g_lightDir", &lightDir);

    g_pd3dDevice->BeginScene();
    {
        UINT np = 0; g_pEffect->Begin(&np, 0); g_pEffect->BeginPass(0);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            // テクスチャがあればセット
            if (g_pTextures[i])
            {
                g_pEffect->SetTexture("texture1", g_pTextures[i]);
            }

            g_pEffect->CommitChanges();
            g_pMesh->DrawSubset(i);
        }

        g_pEffect->EndPass(); g_pEffect->End();
    }
    g_pd3dDevice->EndScene();

    SAFE_RELEASE(sceneRT); // RT0 はこのあと各パスで切替

    //==============================================================
    // 1) Bright Pass（g_pSceneTex → g_pBrightTex）
    //==============================================================
    g_pBloomEffect->SetTechnique("BrightPass");
    g_pBloomEffect->SetTexture("g_SrcTex", g_pSceneTex);

    SetTexelSizeFromTexture(g_pSceneTex);

    g_pBloomEffect->SetFloat("g_Threshold", g_fThreshold);

    LPDIRECT3DSURFACE9 surf = NULL;
    g_pBrightTex->GetSurfaceLevel(0, &surf);
    g_pd3dDevice->SetRenderTarget(0, surf);

    g_pd3dDevice->BeginScene();
    DrawFullScreenQuadCurrentRT(g_pBloomEffect);
    g_pd3dDevice->EndScene();

    SAFE_RELEASE(surf);

    //==============================================================
    // 2) Down チェーン（Bright → 1/2 → 1/4 → 1/8）
    //==============================================================
    LPDIRECT3DTEXTURE9 src = g_pBrightTex;
    for (int i = 0; i < kLevelRange; ++i)
    {
        g_pBloomEffect->SetTechnique("Down");
        g_pBloomEffect->SetTexture("g_SrcTex", src);
        SetTexelSizeFromTexture(src);

        g_texDown[i]->GetSurfaceLevel(0, &surf);
        g_pd3dDevice->SetRenderTarget(0, surf);

        g_pd3dDevice->BeginScene();

        DrawFullScreenQuadCurrentRT(g_pBloomEffect);

        g_pd3dDevice->EndScene();
        SAFE_RELEASE(surf);

        src = g_texDown[i];
    }

    // ===== 2.5) 低解像度スターバースト生成 =====
    {
        // 出力先: g_pStreakTex
        LPDIRECT3DSURFACE9 streakSurface = NULL;
        g_pStreakTex->GetSurfaceLevel(0, &streakSurface);
        g_pd3dDevice->SetRenderTarget(0, streakSurface);

        // クリア（加算合成で蓄積するため黒）
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        // 入力は Bright の低解像度
        LPDIRECT3DTEXTURE9 streakSrc = g_texDown[kStreakLevel];

        g_pBloomEffect->SetTechnique("StreakDirectional");
        g_pBloomEffect->SetTexture("g_SrcTex", streakSrc);
        SetTexelSizeFromTexture(streakSrc);

        // パラメータ設定
        g_pBloomEffect->SetFloat("g_StreakDecay",     g_fStreakDecay);
        g_pBloomEffect->SetFloat("g_StreakStep",      g_fStreakStep);
        g_pBloomEffect->SetFloat("g_StreakGain",      g_fStreakGain);

        // 加算ブレンド有効化
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_ONE);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

        // 0°
        {
            D3DXVECTOR4 dir(1.0f, 0.0f, 0.0f, 0.0f);
            g_pBloomEffect->SetVector("g_Direction", &dir);

            g_pd3dDevice->BeginScene();
            DrawFullScreenQuadCurrentRT(g_pBloomEffect);
            g_pd3dDevice->EndScene();
        }

        // 45°
        {
            const float invSqrt2 = 0.70710678f;
            D3DXVECTOR4 dir(invSqrt2, invSqrt2, 0.0f, 0.0f);
            g_pBloomEffect->SetVector("g_Direction", &dir);

            g_pd3dDevice->BeginScene();
            DrawFullScreenQuadCurrentRT(g_pBloomEffect);
            g_pd3dDevice->EndScene();
        }

        // 135°
        {
            const float invSqrt2 = 0.70710678f;
            D3DXVECTOR4 dir(-invSqrt2, invSqrt2, 0.0f, 0.0f);
            g_pBloomEffect->SetVector("g_Direction", &dir);

            g_pd3dDevice->BeginScene();
            DrawFullScreenQuadCurrentRT(g_pBloomEffect);
            g_pd3dDevice->EndScene();
        }

        // ブレンド無効化
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

        SAFE_RELEASE(streakSurface);
    }

    //==============================================================
    // 3) Up チェーン（1/8 → 1/4 → 1/2 に足し戻し）
    //==============================================================
    const int last = kLevelRange - 1;

    // 最下段コピー相当
    g_pBloomEffect->SetTechnique("Upsample");
    g_pBloomEffect->SetTexture("g_SrcTex",  g_texDown[last]);
    g_pBloomEffect->SetTexture("g_SrcTex2", NULL);

    SetTexelSizeFromTexture(g_texDown[last]);

    g_texUp[last]->GetSurfaceLevel(0, &surf);
    g_pd3dDevice->SetRenderTarget(0, surf);
    g_pd3dDevice->BeginScene();

    DrawFullScreenQuadCurrentRT(g_pBloomEffect);

    g_pd3dDevice->EndScene();
    SAFE_RELEASE(surf);

    // 段を上がりながら加算（Up[i] + Down[i-1]）
    for (int i = last; i >= 1; --i)
    {
        g_pBloomEffect->SetTechnique("Upsample");
        g_pBloomEffect->SetTexture("g_SrcTex",  g_texUp[i]);
        g_pBloomEffect->SetTexture("g_SrcTex2", g_texDown[i - 1]);
        SetTexelSizeFromTexture(g_texUp[i]);

        g_texUp[i - 1]->GetSurfaceLevel(0, &surf);
        g_pd3dDevice->SetRenderTarget(0, surf);
        g_pd3dDevice->BeginScene();
        DrawFullScreenQuadCurrentRT(g_pBloomEffect);
        g_pd3dDevice->EndScene();
        SAFE_RELEASE(surf);
    }

    //==============================================================
    // 4) 合成（Scene + Intensity * Up[0] + Starburst）
    //==============================================================

    g_pd3dDevice->SetRenderTarget(0, oldRT);

    g_pBloomEffect->SetTechnique("Combine");
    g_pBloomEffect->SetTexture("g_SceneTex",  g_pSceneTex);
    g_pBloomEffect->SetTexture("g_SrcTex",    g_texUp[0]);   // Bloom（フル解像度）
    g_pBloomEffect->SetTexture("g_StreakTex", g_pStreakTex); // ★ Starburst（低解像度）
    g_pBloomEffect->SetFloat("g_Intensity",   g_fIntensity);
    g_pBloomEffect->SetFloat("g_StreakIntensity", g_fStreakIntensity);

    g_pd3dDevice->BeginScene();
    DrawFullScreenQuadCurrentRT(g_pBloomEffect);
    g_pd3dDevice->EndScene();

    // 画面に出す
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

    SAFE_RELEASE(oldRT);
}

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        g_bClose = true;
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
