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
LPD3DXFONT g_pFont = NULL;
LPD3DXMESH g_pMesh = NULL;
std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_pEffect = NULL;        // simple.fx
LPD3DXEFFECT g_pBloomEffect = NULL;   // bloom.fx
bool g_bClose = false;

// --- Bloom 用リソース ---
LPDIRECT3DTEXTURE9 g_pSceneTex = NULL;
LPDIRECT3DSURFACE9 g_pSceneSurf = NULL;

LPDIRECT3DTEXTURE9 g_pBrightTex = NULL;
LPDIRECT3DSURFACE9 g_pBrightSurf = NULL;

LPDIRECT3DTEXTURE9 g_pBlurTex = NULL;
LPDIRECT3DSURFACE9 g_pBlurSurf = NULL;

LPDIRECT3DSURFACE9 g_pBackBuffer = NULL;

struct SCREENVERTEX {
    float x, y, z, rhw;
    float u, v;
};
#define D3DFVF_SCREENVERTEX (D3DFVF_XYZRHW | D3DFVF_TEX1)

static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static void InitD3D(HWND hWnd);
static void Cleanup();
static void Render();
static void DrawFullScreenQuad(LPDIRECT3DTEXTURE9 tex, LPD3DXEFFECT effect, const char* technique);
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
    SetRect(&rect, 0, 0, 640, 480);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0;
    rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Hello DirectX9 Bloom Sample (3Pass)"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             rect.right,
                             rect.bottom,
                             NULL,
                             NULL,
                             wc.hInstance,
                             NULL);

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
            Sleep(16);
            Render();
        }

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();

    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y)
{
    RECT rect = { X, Y, 0, 0 };
    HRESULT hResult = pFont->DrawText(NULL,
                                      text,
                                      -1,
                                      &rect,
                                      DT_LEFT | DT_NOCLIP,
                                      D3DCOLOR_ARGB(255, 0, 0, 0));
    assert((int)hResult >= 0);
}

void InitD3D(HWND hWnd)
{
    HRESULT hResult = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;

    hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                   D3DDEVTYPE_HAL,
                                   hWnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &d3dpp,
                                   &g_pd3dDevice);
    if (FAILED(hResult))
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_HAL,
                                       hWnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                       &d3dpp,
                                       &g_pd3dDevice);
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

    // simple.fx
    hResult = D3DXCreateEffectFromFile(g_pd3dDevice, _T("simple.fx"), NULL, NULL,
                                       D3DXSHADER_DEBUG, NULL, &g_pEffect, NULL);
    assert(hResult == S_OK);

    // bloom.fx
    hResult = D3DXCreateEffectFromFile(g_pd3dDevice, _T("bloom.fx"), NULL, NULL,
                                       D3DXSHADER_DEBUG, NULL, &g_pBloomEffect, NULL);
    assert(hResult == S_OK);

    // 各テクスチャ作成
    hResult = D3DXCreateTexture(g_pd3dDevice, 640, 480, 1,
                                D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT, &g_pSceneTex);
    g_pSceneTex->GetSurfaceLevel(0, &g_pSceneSurf);

    hResult = D3DXCreateTexture(g_pd3dDevice, 640, 480, 1,
                                D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT, &g_pBrightTex);
    g_pBrightTex->GetSurfaceLevel(0, &g_pBrightSurf);

    hResult = D3DXCreateTexture(g_pd3dDevice, 640, 480, 1,
                                D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT, &g_pBlurTex);
    g_pBlurTex->GetSurfaceLevel(0, &g_pBlurSurf);

    // バックバッファ保持
    g_pd3dDevice->GetRenderTarget(0, &g_pBackBuffer);
}

void Cleanup()
{
    for (auto& texture : g_pTextures)
    {
        SAFE_RELEASE(texture);
    }

    SAFE_RELEASE(g_pMesh);
    SAFE_RELEASE(g_pEffect);
    SAFE_RELEASE(g_pBloomEffect);
    SAFE_RELEASE(g_pFont);

    SAFE_RELEASE(g_pSceneSurf);
    SAFE_RELEASE(g_pSceneTex);

    SAFE_RELEASE(g_pBrightSurf);
    SAFE_RELEASE(g_pBrightTex);

    SAFE_RELEASE(g_pBlurSurf);
    SAFE_RELEASE(g_pBlurTex);

    SAFE_RELEASE(g_pBackBuffer);
    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void DrawFullScreenQuad(LPDIRECT3DTEXTURE9 tex, LPD3DXEFFECT effect, const char* technique)
{
    g_pd3dDevice->SetFVF(D3DFVF_SCREENVERTEX);
    SCREENVERTEX vertices[4] =
    {
        { -0.5f, -0.5f, 0, 1, 0, 0 },
        { 639.5f, -0.5f, 0, 1, 1, 0 },
        { -0.5f, 479.5f, 0, 1, 0, 1 },
        { 639.5f, 479.5f, 0, 1, 1, 1 },
    };

    effect->SetTechnique(technique);
    effect->SetTexture("g_SrcTex", tex);

    UINT nPass;
    effect->Begin(&nPass, 0);
    effect->BeginPass(0);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(SCREENVERTEX));
    effect->EndPass();
    effect->End();
}

void Render()
{
    // ブラー用のテクセルサイズを設定 (解像度依存)
    D3DXVECTOR4 texelSize(1.0f / 640.0f, 1.0f / 480.0f, 0, 0);
    g_pBloomEffect->SetVector("g_TexelSize", &texelSize);

    HRESULT hResult = E_FAIL;
    static float f = 0.0f;
    f += 0.025f;

    // (1) シーンをテクスチャに描画
    g_pd3dDevice->SetRenderTarget(0, g_pSceneSurf);
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                        D3DCOLOR_XRGB(10, 10, 10), 1.0f, 0);
    g_pd3dDevice->BeginScene();

    D3DXMATRIX mat, View, Proj;
    D3DXMatrixPerspectiveFovLH(&Proj, D3DXToRadian(45), 640.0f / 480.0f, 1.0f, 10000.0f);
    D3DXVECTOR3 vec1(10 * sinf(f), 10, -10 * cosf(f));
    D3DXVECTOR3 vec2(0, 0, 0);
    D3DXVECTOR3 vec3(0, 1, 0);
    D3DXMatrixLookAtLH(&View, &vec1, &vec2, &vec3);
    D3DXMatrixIdentity(&mat);
    mat = mat * View * Proj;
    g_pEffect->SetMatrix("g_matWorldViewProj", &mat);

    TCHAR msg[100];
    _tcscpy_s(msg, 100, _T("ブルーム付きサンプル"));
    TextDraw(g_pFont, msg, 0, 0);

    g_pEffect->SetTechnique("Technique1");
    UINT numPass;
    g_pEffect->Begin(&numPass, 0);
    g_pEffect->BeginPass(0);

    for (DWORD i = 0; i < g_dwNumMaterials; i++)
    {
        g_pEffect->SetTexture("texture1", g_pTextures[i]);
        g_pEffect->CommitChanges();
        g_pMesh->DrawSubset(i);
    }

    g_pEffect->EndPass();
    g_pEffect->End();
    g_pd3dDevice->EndScene();

    // (2) 輝度抽出
    g_pd3dDevice->SetRenderTarget(0, g_pBrightSurf);
    g_pd3dDevice->BeginScene();
    DrawFullScreenQuad(g_pSceneTex, g_pBloomEffect, "BrightPass");
    g_pd3dDevice->EndScene();

    // (3) ブラー
    g_pd3dDevice->SetRenderTarget(0, g_pBlurSurf);
    g_pd3dDevice->BeginScene();
    DrawFullScreenQuad(g_pBrightTex, g_pBloomEffect, "Blur");
    g_pd3dDevice->EndScene();

    // (4) 合成 (最終出力)
    g_pd3dDevice->SetRenderTarget(0, g_pBackBuffer);
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    g_pd3dDevice->BeginScene();

    g_pBloomEffect->SetTexture("g_SceneTex", g_pSceneTex);
    g_pBloomEffect->SetTexture("g_BlurTex", g_pBlurTex);
    DrawFullScreenQuad(NULL, g_pBloomEffect, "Combine");


    g_pd3dDevice->EndScene();

    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
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
