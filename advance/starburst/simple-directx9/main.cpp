#pragma comment(lib, "d3d9.lib")
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment(lib, "d3dx9d.lib")
#else
#pragma comment(lib, "d3dx9.lib")
#endif

#include <d3d9.h>
#include <d3dx9.h>
#include <tchar.h>
#include <cassert>
#include <vector>
#include <string>

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

struct ScreenVertex
{
    float x;
    float y;
    float z;
    float rhw;
    float u;
    float v;
};

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;

LPD3DXMESH g_pMesh = NULL;
std::vector<D3DMATERIAL9> g_materials;
std::vector<LPDIRECT3DTEXTURE9> g_meshTextures;
DWORD g_numMaterials = 0;

LPD3DXEFFECT g_fxBasic = NULL;
LPD3DXEFFECT g_fxStarburst = NULL;

LPDIRECT3DTEXTURE9 g_texBasic = NULL;
LPDIRECT3DTEXTURE9 g_texStreakSrc = NULL;
LPDIRECT3DTEXTURE9 g_texStreak = NULL;

bool g_shouldClose = false;

float g_time = 0.0f;
float g_threshold = 0.7f;

float g_streakIntensity = 1.0f;

// 減衰
float g_streakDecay = 0.86f;

// 隣のピクセルを見るときの幅
float g_streakStep  = 1.5f;

// よくわからない。
float g_streakGain  = 2.0f;

// どれくらい小さいテクスチャで行うか（2=1/2, 4=1/4, 8=1/8, 16=1/16）
int g_nStreakDownscale = 4;

static void SetTexelSizeFromTexture(LPDIRECT3DTEXTURE9 texture,
                                    LPD3DXEFFECT effect,
                                    const char* paramName);

static void SetRenderTargetFromTexture(LPDIRECT3DTEXTURE9 texture);
static void SetRenderTargetToBackbuffer();
static void DrawFullScreenQuadCurrentRT(LPD3DXEFFECT effect);
static void InitD3D(HWND hWnd);
static void Cleanup();
static void Render();

static LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            g_shouldClose = true;
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

extern int WINAPI WinMain(_In_ HINSTANCE hInstance,
                          _In_opt_ HINSTANCE hPrevInstance,
                          _In_ LPSTR lpCmdLine,
                          _In_ int nShowCmd);

int WINAPI WinMain(_In_ HINSTANCE hInstance,
                   _In_opt_ HINSTANCE hPrevInstance,
                   _In_ LPSTR lpCmdLine,
                   _In_ int nShowCmd)
{
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = _T("StarburstWindow");

    ATOM atom = RegisterClassEx(&wc);
    assert(atom != 0);

    RECT rect = { 0, 0, 1600, 900 };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindow(_T("StarburstWindow"),
                             _T("Starburst"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left,
                             rect.bottom - rect.top,
                             NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    InitD3D(hWnd);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (msg.message != WM_QUIT && !g_shouldClose)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Render();
        }
    }

    Cleanup();
    UnregisterClass(_T("StarburstWindow"), wc.hInstance);
    return 0;
}

static void InitD3D(HWND hWnd)
{
    HRESULT hr = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.hDeviceWindow = hWnd;

    hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                              D3DCREATE_HARDWARE_VERTEXPROCESSING,
                              &pp, &g_pd3dDevice);
    if (FAILED(hr))
    {
        hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                  &pp, &g_pd3dDevice);
        assert(SUCCEEDED(hr));
    }

    LPD3DXBUFFER materialBuffer = NULL;
    hr = D3DXLoadMeshFromX(_T("cube.x"), D3DXMESH_SYSTEMMEM, g_pd3dDevice,
                           NULL, &materialBuffer, NULL, &g_numMaterials, &g_pMesh);
    assert(SUCCEEDED(hr));

    D3DXMATERIAL* d3dxMaterials = (D3DXMATERIAL*)materialBuffer->GetBufferPointer();
    g_materials.resize(g_numMaterials);
    g_meshTextures.resize(g_numMaterials, NULL);

    for (DWORD i = 0; i < g_numMaterials; i++)
    {
        g_materials[i] = d3dxMaterials[i].MatD3D;
        g_materials[i].Ambient = g_materials[i].Diffuse;

        if (d3dxMaterials[i].pTextureFilename != NULL &&
            lstrlenA(d3dxMaterials[i].pTextureFilename) > 0)
        {
            hr = D3DXCreateTextureFromFileA(g_pd3dDevice,
                                            d3dxMaterials[i].pTextureFilename,
                                            &g_meshTextures[i]);
        }
    }
    SAFE_RELEASE(materialBuffer);

    hr = D3DXCreateEffectFromFile(g_pd3dDevice, _T("simple.fx"),
                                  NULL, NULL, D3DXSHADER_DEBUG, NULL,
                                  &g_fxBasic, NULL);
    assert(SUCCEEDED(hr));

    hr = D3DXCreateEffectFromFile(g_pd3dDevice, _T("starburst.fx"),
                                  NULL, NULL, D3DXSHADER_DEBUG, NULL,
                                  &g_fxStarburst, NULL);
    assert(SUCCEEDED(hr));

    LPDIRECT3DSURFACE9 backBuffer = NULL;
    g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);

    D3DSURFACE_DESC bbDesc { };
    backBuffer->GetDesc(&bbDesc);
    SAFE_RELEASE(backBuffer);

    int frameWidth  = (int)bbDesc.Width;
    int frameHeight = (int)bbDesc.Height;

    hr = D3DXCreateTexture(g_pd3dDevice, frameWidth, frameHeight, 1,
                           D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                           D3DPOOL_DEFAULT, &g_texBasic);
    assert(SUCCEEDED(hr));

    int smallWidth  = (frameWidth  / g_nStreakDownscale);
    int smallHeight = (frameHeight / g_nStreakDownscale);

    if (smallWidth < 1)
    {
        smallWidth = 1;
    }

    if (smallHeight < 1)
    {
        smallHeight = 1;
    }

    hr = D3DXCreateTexture(g_pd3dDevice, smallWidth, smallHeight, 1,
                           D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                           D3DPOOL_DEFAULT, &g_texStreakSrc);
    assert(SUCCEEDED(hr));

    hr = D3DXCreateTexture(g_pd3dDevice, smallWidth, smallHeight, 1,
                           D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                           D3DPOOL_DEFAULT, &g_texStreak);
    assert(SUCCEEDED(hr));
}

static void Cleanup()
{
    for (size_t i = 0; i < g_meshTextures.size(); ++i)
    {
        SAFE_RELEASE(g_meshTextures[i]);
    }

    SAFE_RELEASE(g_pMesh);
    SAFE_RELEASE(g_fxBasic);
    SAFE_RELEASE(g_fxStarburst);

    SAFE_RELEASE(g_texBasic);
    SAFE_RELEASE(g_texStreakSrc);
    SAFE_RELEASE(g_texStreak);

    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

static void Render()
{
    g_time += 0.01f;

    LPDIRECT3DSURFACE9 oldRT = NULL;
    g_pd3dDevice->GetRenderTarget(0, &oldRT);

    SetRenderTargetFromTexture(g_texBasic);
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                        D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    D3DXMATRIX matWorld;
    D3DXMATRIX matView;
    D3DXMATRIX matProj;
    D3DXMATRIX matWVP;

    D3DXMatrixRotationY(&matWorld, g_time);

    D3DXVECTOR3 eye(0.0f, 2.2f, -5.0f);
    D3DXVECTOR3 at(0.0f, 0.8f, 0.0f);
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);

    D3DXMatrixLookAtLH(&matView, &eye, &at, &up);

    LPDIRECT3DSURFACE9 sceneSurface = NULL;
    g_texBasic->GetSurfaceLevel(0, &sceneSurface);
    D3DSURFACE_DESC sceneDesc;
    sceneSurface->GetDesc(&sceneDesc);
    SAFE_RELEASE(sceneSurface);

    float aspect = (float)sceneDesc.Width / (float)sceneDesc.Height;
    D3DXMatrixPerspectiveFovLH(&matProj, D3DXToRadian(60.0f), aspect, 0.1f, 100.0f);

    matWVP = matWorld * matView * matProj;

    D3DXVECTOR4 lightDir(0.5f, 1.0f, 0.2f, 0.0f);
    D3DXVec4Normalize(&lightDir, &lightDir);
    g_fxBasic->SetVector("g_lightDir", &lightDir);


    //-------------------------------------------------------------
    // 通常の描画
    //-------------------------------------------------------------
    {
        g_pd3dDevice->BeginScene();

        g_fxBasic->SetTechnique("Technique1");
        g_fxBasic->SetMatrix("g_matWorldViewProj", &matWVP);

        UINT passCount = 0;
        g_fxBasic->Begin(&passCount, 0);
        g_fxBasic->BeginPass(0);

        for (DWORD i = 0; i < g_numMaterials; ++i)
        {
            if (g_meshTextures[i] != NULL)
            {
                g_fxBasic->SetTexture("texture1", g_meshTextures[i]);
            }
            g_fxBasic->CommitChanges();
            g_pMesh->DrawSubset(i);
        }

        g_fxBasic->EndPass();
        g_fxBasic->End();

        g_pd3dDevice->EndScene();
    }

    //-------------------------------------------------------------
    // 明るい部分を抽出
    //-------------------------------------------------------------
    {
        SetRenderTargetFromTexture(g_texStreakSrc);
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        g_fxStarburst->SetTechnique("BrightDown");
        g_fxStarburst->SetTexture("g_SrcTex", g_texBasic);
        g_fxStarburst->SetFloat("g_Threshold", g_threshold);
        SetTexelSizeFromTexture(g_texBasic, g_fxStarburst, "g_TexelSize");

        g_pd3dDevice->BeginScene();
        DrawFullScreenQuadCurrentRT(g_fxStarburst);
        g_pd3dDevice->EndScene();
    }

    //-------------------------------------------------------------
    // 光の筋を描画
    //-------------------------------------------------------------
    {

        SetRenderTargetFromTexture(g_texStreak);
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

        g_fxStarburst->SetTechnique("StreakDirectional");
        g_fxStarburst->SetTexture("g_SrcTex", g_texStreakSrc);
        g_fxStarburst->SetFloat("g_StreakDecay", g_streakDecay);
        g_fxStarburst->SetFloat("g_StreakStep",  g_streakStep);
        g_fxStarburst->SetFloat("g_StreakGain",  g_streakGain);
        SetTexelSizeFromTexture(g_texStreakSrc, g_fxStarburst, "g_TexelSize");

        {
            D3DXVECTOR4 dirRight(1.0f, 0.0f, 0.0f, 0.0f);
            g_fxStarburst->SetVector("g_Direction", &dirRight);
            g_pd3dDevice->BeginScene();
            DrawFullScreenQuadCurrentRT(g_fxStarburst);
            g_pd3dDevice->EndScene();
        }

        {
            const float r = 0.70710678f;
            D3DXVECTOR4 dir45(r, r, 0.0f, 0.0f);
            g_fxStarburst->SetVector("g_Direction", &dir45);
            g_pd3dDevice->BeginScene();
            DrawFullScreenQuadCurrentRT(g_fxStarburst);
            g_pd3dDevice->EndScene();
        }

        {
            const float r = 0.70710678f;
            D3DXVECTOR4 dir135(-r, r, 0.0f, 0.0f);
            g_fxStarburst->SetVector("g_Direction", &dir135);
            g_pd3dDevice->BeginScene();
            DrawFullScreenQuadCurrentRT(g_fxStarburst);
            g_pd3dDevice->EndScene();
        }

    }

    //-------------------------------------------------------------
    // 結果を合成
    //-------------------------------------------------------------
    {
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

        SetRenderTargetToBackbuffer();
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        g_fxStarburst->SetTechnique("Combine");
        g_fxStarburst->SetTexture("g_SceneTex",  g_texBasic);
        g_fxStarburst->SetTexture("g_StreakTex", g_texStreak);
        g_fxStarburst->SetFloat("g_StreakIntensity", g_streakIntensity);

        g_pd3dDevice->BeginScene();
        DrawFullScreenQuadCurrentRT(g_fxStarburst);
        g_pd3dDevice->EndScene();
    }

    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

    SAFE_RELEASE(oldRT);
}

static void SetTexelSizeFromTexture(LPDIRECT3DTEXTURE9 texture, LPD3DXEFFECT effect, const char* paramName)
{
    D3DSURFACE_DESC desc;
    texture->GetLevelDesc(0, &desc);

    D3DXVECTOR4 texelSize;
    texelSize.x = 1.0f / (float)desc.Width;
    texelSize.y = 1.0f / (float)desc.Height;
    texelSize.z = 0.0f;
    texelSize.w = 0.0f;

    effect->SetVector(paramName, &texelSize);
}

static void SetRenderTargetFromTexture(LPDIRECT3DTEXTURE9 texture)
{
    LPDIRECT3DSURFACE9 surface = NULL;
    texture->GetSurfaceLevel(0, &surface);
    g_pd3dDevice->SetRenderTarget(0, surface);

    D3DSURFACE_DESC desc { };
    surface->GetDesc(&desc);

    D3DVIEWPORT9 vp { };
    vp.X = 0;
    vp.Y = 0;
    vp.Width  = desc.Width;
    vp.Height = desc.Height;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    SAFE_RELEASE(surface);
}

static void SetRenderTargetToBackbuffer()
{
    LPDIRECT3DSURFACE9 backBuffer = NULL;
    g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    g_pd3dDevice->SetRenderTarget(0, backBuffer);

    D3DSURFACE_DESC desc { };
    backBuffer->GetDesc(&desc);

    D3DVIEWPORT9 vp { };
    vp.X = 0;
    vp.Y = 0;
    vp.Width  = desc.Width;
    vp.Height = desc.Height;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    SAFE_RELEASE(backBuffer);
}

static void DrawFullScreenQuadCurrentRT(LPD3DXEFFECT effect)
{
    g_pd3dDevice->SetVertexShader(NULL);
    g_pd3dDevice->SetVertexDeclaration(NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);

    LPDIRECT3DSURFACE9 renderTarget = NULL;
    g_pd3dDevice->GetRenderTarget(0, &renderTarget);

    D3DSURFACE_DESC desc;
    renderTarget->GetDesc(&desc);
    SAFE_RELEASE(renderTarget);

    ScreenVertex vertices[4] =
    {
        { -0.5f,                 -0.5f,                  0.0f, 1.0f, 0.0f, 0.0f },
        { desc.Width - 0.5f,     -0.5f,                  0.0f, 1.0f, 1.0f, 0.0f },
        { -0.5f,                 desc.Height - 0.5f,     0.0f, 1.0f, 0.0f, 1.0f },
        { desc.Width - 0.5f,     desc.Height - 0.5f,     0.0f, 1.0f, 1.0f, 1.0f },
    };

    g_pd3dDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

    UINT passCount = 0;
    effect->Begin(&passCount, 0);
    effect->BeginPass(0);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ScreenVertex));
    effect->EndPass();
    effect->End();

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

