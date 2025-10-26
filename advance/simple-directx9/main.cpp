#pragma comment( lib, "d3d9.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif

#pragma comment(lib, "winmm.lib")	// winmm.libを使用する

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
LPD3DXFONT g_pFont = NULL;
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

// ★ 追加：ピラミッド（3段）
static const int kLevels = 12; // 1/2, 1/4, 1/8
LPDIRECT3DTEXTURE9 g_pDown[kLevels] = {0};
LPDIRECT3DTEXTURE9 g_pUp[kLevels]   = {0};

struct SCREENVERTEX {
    float x, y, z, rhw;
    float u, v;
};
#define D3DFVF_SCREENVERTEX (D3DFVF_XYZRHW | D3DFVF_TEX1)

static void SetTexelSizeFromTexture(LPDIRECT3DTEXTURE9 tex)
{
    D3DSURFACE_DESC sd; tex->GetLevelDesc(0, &sd);
    D3DXVECTOR4 t(1.0f / sd.Width, 1.0f / sd.Height, 0, 0);
    g_pBloomEffect->SetVector("g_TexelSize", &t);
}

static void DrawFullScreenQuadAutoUV(LPD3DXEFFECT fx)
{
    // 現在の RT0 のサイズに合わせたスクリーンクアッド（半テクセル補正つき）
    g_pd3dDevice->SetVertexShader(NULL);
    g_pd3dDevice->SetVertexDeclaration(NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);

    LPDIRECT3DSURFACE9 rt = NULL;
    g_pd3dDevice->GetRenderTarget(0, &rt);
    D3DSURFACE_DESC sd; rt->GetDesc(&sd);
    SAFE_RELEASE(rt);

    const float w = (float)sd.Width;
    const float h = (float)sd.Height;

    SCREENVERTEX v[4] =
    {
        { -0.5f,   -0.5f, 0, 1, 0, 0 },
        { w-0.5f,  -0.5f, 0, 1, 1, 0 },
        { -0.5f,  h-0.5f, 0, 1, 0, 1 },
        { w-0.5f, h-0.5f, 0, 1, 1, 1 },
    };

    g_pd3dDevice->SetFVF(D3DFVF_SCREENVERTEX);

    UINT n = 0; fx->Begin(&n, 0); fx->BeginPass(0);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(SCREENVERTEX));
    fx->EndPass(); fx->End();

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}


static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static void InitD3D(HWND hWnd);
static void Cleanup();
static void Render();
static void DrawFullScreenQuad(LPDIRECT3DTEXTURE9 tex, LPD3DXEFFECT effect, const char* technique);
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// === Helper: テクスチャからRTを一時取得して設定（即Release） ===
static void SetRTFromTex(LPDIRECT3DTEXTURE9 tex)
{
    LPDIRECT3DSURFACE9 rt = NULL;
    tex->GetSurfaceLevel(0, &rt);                 // AddRef 済みで返る
    g_pd3dDevice->SetRenderTarget(0, rt);         // Device 側が参照を保持
    SAFE_RELEASE(rt);                             // 即ReleaseでOK
}

static void SetRTBackBuffer()
{
    LPDIRECT3DSURFACE9 bb = NULL;
    g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    g_pd3dDevice->SetRenderTarget(0, bb);
    SAFE_RELEASE(bb);
}

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

    RECT rect; SetRect(&rect, 0, 0, 1600, 900);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0; rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Hello DirectX9 Bloom Sample (3Pass)"),
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
        else
        {
            Render();
        }
        if (g_bClose) break;
    }

    Cleanup();
    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y)
{
    RECT rect = { X, Y, 0, 0 };
    HRESULT hResult = pFont->DrawText(NULL, text, -1, &rect, DT_LEFT | DT_NOCLIP,
                                      D3DCOLOR_ARGB(255, 0, 0, 0));
    assert((int)hResult >= 0);
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

    // フォント
    hResult = D3DXCreateFont(g_pd3dDevice, 20, 0, FW_HEAVY, 1, FALSE,
                             SHIFTJIS_CHARSET, OUT_TT_ONLY_PRECIS,
                             CLEARTYPE_NATURAL_QUALITY,
                             FF_DONTCARE, _T("ＭＳ ゴシック"), &g_pFont);
    assert(hResult == S_OK);

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

    // simple.fx / bloom.fx
    hResult = D3DXCreateEffectFromFile(g_pd3dDevice, _T("simple.fx"), NULL, NULL,
                                       D3DXSHADER_DEBUG, NULL, &g_pEffect, NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice, _T("bloom.fx"), NULL, NULL,
                                       D3DXSHADER_DEBUG, NULL, &g_pBloomEffect, NULL);
    assert(hResult == S_OK);

    // 各テクスチャ作成（サーフェイスは保持しない）
    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &g_pSceneTex);

    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &g_pBrightTex);

    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &g_pBlurTexH);

    D3DXCreateTexture(g_pd3dDevice, 1600, 900, 1,
                      D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &g_pBlurTexV);

    int w = 1600, h = 900;
    for (int i = 0; i < kLevels; ++i)
    {
        w = (std::max)(1, w / 2);
        h = (std::max)(1, h / 2);
        D3DXCreateTexture(g_pd3dDevice, w, h, 1, D3DUSAGE_RENDERTARGET,
                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_pDown[i]);
        D3DXCreateTexture(g_pd3dDevice, w, h, 1, D3DUSAGE_RENDERTARGET,
                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_pUp[i]);
    }
}

void Cleanup()
{
    for (auto& texture : g_pTextures)
    {
        SAFE_RELEASE(texture);
    }

    for (int i = 0; i < kLevels; ++i)
    {
        SAFE_RELEASE(g_pDown[i]); SAFE_RELEASE(g_pUp[i]);
    }

    SAFE_RELEASE(g_pMesh);
    SAFE_RELEASE(g_pEffect);
    SAFE_RELEASE(g_pBloomEffect);
    SAFE_RELEASE(g_pFont);

    SAFE_RELEASE(g_pSceneTex);
    SAFE_RELEASE(g_pBrightTex);
    SAFE_RELEASE(g_pBlurTexH);
    SAFE_RELEASE(g_pBlurTexV);

    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void DrawFullScreenQuad(LPDIRECT3DTEXTURE9 tex, LPD3DXEFFECT effect, const char* technique)
{
    // 固定機能（FVF）で描くため、前のパスの VS/頂点宣言を解除
    g_pd3dDevice->SetVertexShader(NULL);
    g_pd3dDevice->SetVertexDeclaration(NULL);

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);

    SCREENVERTEX vertices[4] =
    {
        { -0.5f,  -0.5f,   0, 1, 0, 0 },
        { 1600.f - 0.5f, -0.5f,   0, 1, 1, 0 },
        { -0.5f,  900.f - 0.5f,  0, 1, 0, 1 },
        { 1600.f - 0.5f, 900.f - 0.5f,  0, 1, 1, 1 },
    };

    g_pd3dDevice->SetFVF(D3DFVF_SCREENVERTEX);

    effect->SetTechnique(technique);
    if (tex) effect->SetTexture("g_SrcTex", tex);

    UINT nPass = 0;
    effect->Begin(&nPass, 0);
    effect->BeginPass(0);
    // （BeginPass後にVSがバインドされるテクでも、上でNULLにしているので固定機能で通る）
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(SCREENVERTEX));
    effect->EndPass();
    effect->End();

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

// 現在の RT0 サーフェイスのサイズに合わせ、半テクセル補正(-0.5)つきでフルスクリーンクアッドを描く
static void DrawFullScreenQuadCurrentRT(LPD3DXEFFECT fx)
{
    LPDIRECT3DSURFACE9 rt = NULL;
    g_pd3dDevice->GetRenderTarget(0, &rt);
    D3DSURFACE_DESC sd; rt->GetDesc(&sd);
    rt->Release();

    const float w = (float)sd.Width;
    const float h = (float)sd.Height;

    SCREENVERTEX v[4] =
    {
        { -0.5f,   -0.5f, 0, 1, 0, 0 },
        { w-0.5f,  -0.5f, 0, 1, 1, 0 },
        { -0.5f,  h-0.5f, 0, 1, 0, 1 },
        { w-0.5f, h-0.5f, 0, 1, 1, 1 },
    };

    g_pd3dDevice->SetFVF(D3DFVF_SCREENVERTEX);

    UINT n = 0;
    fx->Begin(&n, 0);
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
    g_pd3dDevice->Clear(0, NULL,
        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
        D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    // 行列セット
    D3DXMATRIX W, V, P, WVP;
    D3DXMatrixRotationY(&W, timeGetTime() * 0.001f);
    D3DXVECTOR3 eye(0, 2.2f, -5.0f), at(0, 0.8f, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&V, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&P, D3DXToRadian(60.f), 1600.0f/900.0f, 0.1f, 100.0f);
    WVP = W * V * P;

    // simple.fx（Technique1）でメッシュ描画
    g_pEffect->SetTechnique("Technique1");
    g_pEffect->SetMatrix("g_matWorldViewProj", &WVP);
    D3DXVECTOR4 lightN(0.3f, 1.0f, 0.5f, 0.0f);
    g_pEffect->SetVector("g_lightNormal", &lightN);

    g_pd3dDevice->BeginScene();
    {
        UINT np = 0; g_pEffect->Begin(&np, 0); g_pEffect->BeginPass(0);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            // テクスチャがあればセット
            if (g_pTextures[i]) g_pEffect->SetTexture("texture1", g_pTextures[i]);
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
    SetTexelSizeFromTexture(g_pSceneTex);           // ソースのテクセルサイズ（重要）
    g_pBloomEffect->SetFloat("g_Threshold", 0.3f);  // LDRなら 0.7〜1.0 推奨

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
    for (int i = 0; i < kLevels; ++i)
    {
        g_pBloomEffect->SetTechnique("Down");
        g_pBloomEffect->SetTexture("g_SrcTex", src);
        SetTexelSizeFromTexture(src);

        g_pDown[i]->GetSurfaceLevel(0, &surf);
        g_pd3dDevice->SetRenderTarget(0, surf);
        g_pd3dDevice->BeginScene();
        DrawFullScreenQuadCurrentRT(g_pBloomEffect);
        g_pd3dDevice->EndScene();
        SAFE_RELEASE(surf);

        src = g_pDown[i];
    }

    //==============================================================
    // 3) Up チェーン（1/8 → 1/4 → 1/2 に足し戻し）
    //==============================================================
    const int last = kLevels - 1;

    // 最下段コピー相当
    g_pBloomEffect->SetTechnique("Upsample");
    g_pBloomEffect->SetTexture("g_SrcTex",  g_pDown[last]);
    g_pBloomEffect->SetTexture("g_SrcTex2", NULL);
    SetTexelSizeFromTexture(g_pDown[last]);

    g_pUp[last]->GetSurfaceLevel(0, &surf);
    g_pd3dDevice->SetRenderTarget(0, surf);
    g_pd3dDevice->BeginScene();
    DrawFullScreenQuadCurrentRT(g_pBloomEffect);
    g_pd3dDevice->EndScene();
    SAFE_RELEASE(surf);

    // 段を上がりながら加算（Up[i] + Down[i-1]）
    for (int i = last; i >= 1; --i)
    {
        g_pBloomEffect->SetTechnique("Upsample");
        g_pBloomEffect->SetTexture("g_SrcTex",  g_pUp[i]);
        g_pBloomEffect->SetTexture("g_SrcTex2", g_pDown[i-1]);
        SetTexelSizeFromTexture(g_pUp[i]);

        g_pUp[i-1]->GetSurfaceLevel(0, &surf);
        g_pd3dDevice->SetRenderTarget(0, surf);
        g_pd3dDevice->BeginScene();
        DrawFullScreenQuadCurrentRT(g_pBloomEffect);
        g_pd3dDevice->EndScene();
        SAFE_RELEASE(surf);
    }

    //==============================================================
    // 4) 合成（Scene + Intensity * Up[0]） → バックバッファ
    //==============================================================
    g_pd3dDevice->SetRenderTarget(0, oldRT); // BackBuffer に戻す
    g_pBloomEffect->SetTechnique("Combine");
    g_pBloomEffect->SetTexture("g_SceneTex", g_pSceneTex);
    g_pBloomEffect->SetTexture("g_SrcTex",   g_pUp[0]);
    g_pBloomEffect->SetFloat("g_Intensity",  0.6f);

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
