/**
 * NvFBC Screenshot Viewer — D3D11 fullscreen quad + ImGui FPS overlay.
 */

#include "NvFBC.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <cstdio>
#include <vector>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

 // ============================================================================
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_pRTV = nullptr;

static ID3D11Texture2D* g_pTex = nullptr;
static ID3D11ShaderResourceView* g_pTexSRV = nullptr;
static int g_texW = 0, g_texH = 0;

static ID3D11VertexShader* g_pVS = nullptr;
static ID3D11PixelShader* g_pPS = nullptr;
static ID3D11SamplerState* g_pSampler = nullptr;
static ID3D11RasterizerState* g_pRS = nullptr;

// ============================================================================
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* pBB = nullptr;
            g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB);
            g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_pRTV);
            pBB->Release();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// ============================================================================
static bool InitD3D11(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;   // let DXGI pick
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dContext)))
        return false;

    ID3D11Texture2D* pBB = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB);
    g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_pRTV);
    pBB->Release();

    // VS: fullscreen triangle strip
    const char* vsSrc =
        "void main(uint id:SV_VertexID,out float4 p:SV_POSITION,out float2 uv:TEXCOORD0){"
        "uv=float2(id&1,(id>>1)&1);"
        "p=float4(uv*float2(2,-2)+float2(-1,1),0,1);}";

    // PS: sample texture
    const char* psSrc =
        "Texture2D t:register(t0);SamplerState s:register(s0);"
        "float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{return t.Sample(s,uv);}";

    ID3DBlob* blob = nullptr, * err = nullptr;
    D3DCompile(vsSrc, strlen(vsSrc), "vs", nullptr, nullptr, "main", "vs_4_0", 0, 0, &blob, &err);
    if (blob) { g_pd3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pVS); blob->Release(); }
    else if (err) { printf("[!] VS: %s\n", (char*)err->GetBufferPointer()); err->Release(); return false; }

    D3DCompile(psSrc, strlen(psSrc), "ps", nullptr, nullptr, "main", "ps_4_0", 0, 0, &blob, &err);
    if (blob) { g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pPS); blob->Release(); }
    else if (err) { printf("[!] PS: %s\n", (char*)err->GetBufferPointer()); err->Release(); return false; }

    D3D11_SAMPLER_DESC sdesc = {};
    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    g_pd3dDevice->CreateSamplerState(&sdesc, &g_pSampler);

    D3D11_RASTERIZER_DESC rdesc = {};
    rdesc.FillMode = D3D11_FILL_SOLID;
    rdesc.CullMode = D3D11_CULL_NONE;
    g_pd3dDevice->CreateRasterizerState(&rdesc, &g_pRS);

    printf("[+] D3D11 + shaders ready\n");
    return true;
}

static void CleanupD3D11() {
    if (g_pTexSRV) { g_pTexSRV->Release(); g_pTexSRV = nullptr; }
    if (g_pTex) { g_pTex->Release(); g_pTex = nullptr; }
    if (g_pVS) { g_pVS->Release(); g_pVS = nullptr; }
    if (g_pPS) { g_pPS->Release(); g_pPS = nullptr; }
    if (g_pSampler) { g_pSampler->Release(); g_pSampler = nullptr; }
    if (g_pRS) { g_pRS->Release(); g_pRS = nullptr; }
    if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// ============================================================================
static void UploadTexture(const BYTE* pData, int w, int h) {
    if (w != g_texW || h != g_texH) {
        if (g_pTexSRV) { g_pTexSRV->Release(); g_pTexSRV = nullptr; }
        if (g_pTex) { g_pTex->Release(); g_pTex = nullptr; }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w; desc.Height = h;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        g_pd3dDevice->CreateTexture2D(&desc, nullptr, &g_pTex);
        g_pd3dDevice->CreateShaderResourceView(g_pTex, nullptr, &g_pTexSRV);
        g_texW = w; g_texH = h;
    }
    if (!g_pTex || !pData) return;
    g_pd3dContext->UpdateSubresource(g_pTex, 0, nullptr, pData, 4 * w, 0);
}

// ============================================================================
enum SaveFmt { FMT_BMP, FMT_JPG, FMT_PNG };

static bool SaveImage(const BYTE* pixels, int w, int h, SaveFmt fmt) {
    if (!pixels || w <= 0 || h <= 0) return false;
    UINT dataSize = 4 * (UINT)w * (UINT)h;

    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* ext = (fmt == FMT_BMP) ? "bmp" : (fmt == FMT_JPG) ? "jpg" : "png";
    char name[128];
    sprintf_s(name, "screenshot_%04d%02d%02d_%02d%02d%02d.%s",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext);

    // ============================================================
    // BMP: 直接写入原始 BGRA 数据（Alpha 被忽略）
    // ============================================================
    if (fmt == FMT_BMP) {
        FILE* f = nullptr;
        fopen_s(&f, name, "wb");
        if (!f) return false;

        unsigned int ds = (unsigned int)dataSize;
        unsigned short bfType = 0x4D42;          // "BM"
        unsigned int bfSize = 54 + ds;
        unsigned int bfOffBits = 54;

        // BITMAPFILEHEADER
        fwrite(&bfType, 2, 1, f);
        fwrite(&bfSize, 4, 1, f);
        fwrite("\0\0\0\0", 4, 1, f);             // bfReserved1/2
        fwrite(&bfOffBits, 4, 1, f);

        // BITMAPINFOHEADER (40 bytes)
        unsigned int biSize = 40;
        int biWidth = w;
        int biHeight = -h;                       // 负值 = 自上而下
        unsigned short biPlanes = 1;
        unsigned short biBitCount = 32;
        unsigned int biCompression = 0;          // BI_RGB
        unsigned int biSizeImage = ds;
        int biXPelsPerMeter = 0;
        int biYPelsPerMeter = 0;
        unsigned int biClrUsed = 0;
        unsigned int biClrImportant = 0;

        fwrite(&biSize, 4, 1, f);
        fwrite(&biWidth, 4, 1, f);
        fwrite(&biHeight, 4, 1, f);
        fwrite(&biPlanes, 2, 1, f);
        fwrite(&biBitCount, 2, 1, f);
        fwrite(&biCompression, 4, 1, f);
        fwrite(&biSizeImage, 4, 1, f);
        fwrite(&biXPelsPerMeter, 4, 1, f);
        fwrite(&biYPelsPerMeter, 4, 1, f);
        fwrite(&biClrUsed, 4, 1, f);
        fwrite(&biClrImportant, 4, 1, f);

        fwrite(pixels, 1, ds, f);
        fclose(f);
        printf("[+] Saved: %s (BMP, %dx%d, %.2f MB)\n", name, w, h, ds / (1024.0f * 1024.0f));
        return true;
    }

    // ============================================================
    // JPG / PNG: 使用 WIC 编码器
    // ============================================================
    HRESULT hr;
    IWICImagingFactory* pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) {
        printf("[!] WIC factory failed: 0x%08X\n", hr);
        return false;
    }

    IWICBitmap* pBitmap = nullptr;

    if (fmt == FMT_PNG) {
        // ============================================================
        // PNG: 修复 Alpha 通道 (NvFBC 的 BGRA 中 A=0，需要改为 255)
        // ============================================================
        std::vector<BYTE> fixedPixels(pixels, pixels + dataSize);

        // BGRA 格式，A 在每 4 字节的第 4 个位置 (索引 3)
        for (size_t i = 3; i < fixedPixels.size(); i += 4) {
            fixedPixels[i] = 255;  // 设为完全不透明
        }

        hr = pFactory->CreateBitmapFromMemory(
            w, h,
            GUID_WICPixelFormat32bppBGRA,
            4 * w,
            (UINT)dataSize,
            fixedPixels.data(),
            &pBitmap);

        if (FAILED(hr)) {
            printf("[!] PNG CreateBitmap failed: 0x%08X\n", hr);
            pFactory->Release();
            return false;
        }
    }
    else {
        // ============================================================
        // JPG: 直接使用原始数据 (JPEG 不支持 Alpha，WIC 会忽略)
        // ============================================================
        hr = pFactory->CreateBitmapFromMemory(
            w, h,
            GUID_WICPixelFormat32bppBGRA,
            4 * w,
            (UINT)dataSize,
            (BYTE*)pixels,
            &pBitmap);

        if (FAILED(hr)) {
            printf("[!] JPG CreateBitmap failed: 0x%08X\n", hr);
            pFactory->Release();
            return false;
        }
    }

    // ============================================================
    // 创建输出流
    // ============================================================
    wchar_t wname[256];
    mbstowcs_s(nullptr, wname, 256, name, _TRUNCATE);

    IWICStream* pStream = nullptr;
    hr = pFactory->CreateStream(&pStream);
    if (SUCCEEDED(hr)) {
        hr = pStream->InitializeFromFilename(wname, GENERIC_WRITE);
    }
    if (FAILED(hr)) {
        printf("[!] Stream init failed: 0x%08X\n", hr);
        pBitmap->Release();
        pFactory->Release();
        return false;
    }

    // ============================================================
    // 创建编码器
    // ============================================================
    GUID containerGuid = (fmt == FMT_JPG) ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng;
    IWICBitmapEncoder* pEncoder = nullptr;
    hr = pFactory->CreateEncoder(containerGuid, nullptr, &pEncoder);
    if (SUCCEEDED(hr)) {
        hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
    }
    if (FAILED(hr)) {
        printf("[!] Encoder init failed: 0x%08X\n", hr);
        pStream->Release();
        pBitmap->Release();
        pFactory->Release();
        return false;
    }

    // ============================================================
    // 创建帧编码器
    // ============================================================
    IWICBitmapFrameEncode* pFrame = nullptr;
    IPropertyBag2* pProps = nullptr;
    hr = pEncoder->CreateNewFrame(&pFrame, &pProps);
    if (SUCCEEDED(hr)) {
        // 设置 JPEG 质量 (PNG 忽略此设置)
        if (fmt == FMT_JPG) {
            PROPBAG2 option = {};
            option.pstrName = SysAllocString(L"ImageQuality");
            VARIANT varValue;
            VariantInit(&varValue);
            varValue.vt = VT_R4;
            varValue.fltVal = 1.f;  // 100% 质量
            pProps->Write(1, &option, &varValue);
            VariantClear(&varValue);
        }
        hr = pFrame->Initialize(pProps);
    }
    if (SUCCEEDED(hr)) {
        hr = pFrame->SetSize(w, h);
    }
    if (SUCCEEDED(hr)) {
        hr = pFrame->WriteSource(pBitmap, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = pFrame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = pEncoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        printf("[+] Saved: %s (%s, %dx%d)\n", name,
            (fmt == FMT_JPG) ? "JPEG" : "PNG", w, h);
    }
    else {
        printf("[!] Save failed: %s (0x%08X)\n", name, hr);
    }

    // ============================================================
    // 清理资源
    // ============================================================
    if (pProps) pProps->Release();
    if (pFrame) pFrame->Release();
    if (pEncoder) pEncoder->Release();
    if (pStream) pStream->Release();
    if (pBitmap) pBitmap->Release();
    if (pFactory) pFactory->Release();

    return SUCCEEDED(hr);
}
// ============================================================================
int main() {
    CoInitialize(nullptr);

    printf("========================================\n");
    printf("  NvFBC Screenshot Viewer\n");
    printf("========================================\n\n");

    NvFBC nvfbc;
    if (!nvfbc.Initialize()) { printf("[!] NvFBC init failed\n"); system("pause"); return 1; }

    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), 0, WndProc, 0, 0,
        GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "NvFBCViewer", nullptr };
    RegisterClassExA(&wc);
    RECT r = { 0, 0, 1600, 900 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindowA("NvFBCViewer", "NvFBC Viewer", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!InitD3D11(hWnd)) { printf("[!] D3D11 init failed\n"); system("pause"); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    const BYTE* pFrame = nullptr;
    int capW = 0, capH = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();
    float avgFps = 0.0f, grabMs = 0.0f, uploadMs = 0.0f;
    bool running = true;

    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        if (!running) break;

        auto t0 = std::chrono::high_resolution_clock::now();
        if (nvfbc.GrabFrame(pFrame, capW, capH)) {
            auto t1 = std::chrono::high_resolution_clock::now();
            UploadTexture(pFrame, capW, capH);
            auto t2 = std::chrono::high_resolution_clock::now();
            grabMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            uploadMs = std::chrono::duration<float, std::milli>(t2 - t1).count();

            auto now = t2;
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;
            avgFps = avgFps * 0.95f + (1.0f / dt) * 0.05f;

            // --- Draw D3D11 fullscreen quad ---
            g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
            float clear[4] = {};
            g_pd3dContext->ClearRenderTargetView(g_pRTV, clear);

            D3D11_VIEWPORT vp = { 0, 0, 1600.0f, 900.0f, 0, 1 };
            g_pd3dContext->RSSetViewports(1, &vp);
            g_pd3dContext->RSSetState(g_pRS);
            g_pd3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            g_pd3dContext->IASetInputLayout(nullptr);
            g_pd3dContext->VSSetShader(g_pVS, nullptr, 0);
            g_pd3dContext->PSSetShader(g_pPS, nullptr, 0);
            g_pd3dContext->PSSetShaderResources(0, 1, &g_pTexSRV);
            g_pd3dContext->PSSetSamplers(0, 1, &g_pSampler);
            g_pd3dContext->Draw(4, 0);

            // --- ImGui overlay ---
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::Text("FPS: %.1f", avgFps);
            ImGui::Text("%dx%d", capW, capH);
            ImGui::Text("Grab: %.1f ms  Upload: %.1f ms", grabMs, uploadMs);
            if (ImGui::Button("Save BMP")) SaveImage(pFrame, capW, capH, FMT_BMP);
            ImGui::SameLine();
            if (ImGui::Button("Save JPG")) SaveImage(pFrame, capW, capH, FMT_JPG);
            ImGui::SameLine();
            if (ImGui::Button("Save PNG")) SaveImage(pFrame, capW, capH, FMT_PNG);
            ImGui::End();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_pSwapChain->Present(1, 0);  // no vsync for max fps
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupD3D11();
    nvfbc.Cleanup();
    DestroyWindow(hWnd);
    return 0;
}
