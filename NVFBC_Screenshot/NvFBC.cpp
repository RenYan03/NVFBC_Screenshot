#include "NvFBC.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ============================================================================
// NvFBC implementation
// ============================================================================

NvFBC::NvFBC() : m_hNvFBC(nullptr), m_pInstance(nullptr),
                 m_pD3DDevice(nullptr), m_pD3DContext(nullptr),
                 m_pCaptureBuffer(nullptr), m_bInitialized(false) {}

NvFBC::~NvFBC() { Cleanup(); }

bool NvFBC::IsNvidiaGPUAvailable() {
    return GetModuleHandleA("nvd3dum.dll")  || GetModuleHandleA("nvd3dumx.dll")
        || GetModuleHandleA("nvwgf2um.dll") || GetModuleHandleA("nvwgf2umx.dll");
}

bool NvFBC::FindNvidiaDriverPath(char* outDir, size_t size) {
    const char* mods[] = {
        "nvwgf2um.dll", "nvwgf2umx.dll", "nvd3dum.dll", "nvd3dumx.dll",
        "nvldumd.dll", "nvldumdx.dll", "nvppex.dll", "nvspcap64.dll"
    };
    for (auto m : mods) {
        HMODULE h = GetModuleHandleA(m);
        if (h) {
            GetModuleFileNameA(h, outDir, (DWORD)size);
            char* s = strrchr(outDir, '\\');
            if (s) *(s + 1) = '\0';
            return true;
        }
    }
    return false;
}

bool NvFBC::LoadNvidiaDrivers() {
    bool loaded = IsNvidiaGPUAvailable()
        || GetModuleHandleA("nvldumd.dll") || GetModuleHandleA("nvldumdx.dll")
        || GetModuleHandleA("nvppex.dll")   || GetModuleHandleA("nvspcap64.dll");
    if (loaded) return true;

    IDXGIFactory* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory)))
        return false;

    IDXGIAdapter* pNvAdapter = nullptr;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pNvAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC desc;
        pNvAdapter->GetDesc(&desc);
        printf("[*] Adapter %u: %ls (VendorId=0x%04X)\n", i, desc.Description, desc.VendorId);
        if (desc.VendorId == 0x10DE) break;
        pNvAdapter->Release();
        pNvAdapter = nullptr;
    }
    pFactory->Release();

    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(
        pNvAdapter, pNvAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &m_pD3DDevice, &fl, &m_pD3DContext);
    if (pNvAdapter) pNvAdapter->Release();
    return SUCCEEDED(hr);
}

bool NvFBC::Initialize() {


    // Detect screen resolution once
    HDC hDC = CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
    m_screenW = GetDeviceCaps(hDC, 8);
    m_screenH = GetDeviceCaps(hDC, 10);
    DeleteDC(hDC);
    printf("[+] Screen: %dx%d\n", m_screenW, m_screenH);

    if (!LoadNvidiaDrivers()) {
        printf("[!] Failed to load NVIDIA drivers\n");
        return false;
    }

    SetEnvironmentVariableA("NVFBC_TARGET_ADAPTER", "0");

    char nvDir[MAX_PATH] = {};
    if (FindNvidiaDriverPath(nvDir, sizeof(nvDir)) && nvDir[0]) {
        char path[MAX_PATH];
        sprintf_s(path, "%sNvFBC64.dll", nvDir);
        m_hNvFBC = LoadLibraryA(path);
    }
    if (!m_hNvFBC) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat_s(path, "\\NvFBC64.dll");
        m_hNvFBC = LoadLibraryA(path);
    }
    if (!m_hNvFBC) m_hNvFBC = LoadLibraryA("NvFBC64.dll");
    if (!m_hNvFBC) {
        printf("[!] Cannot load NvFBC64.dll\n");
        return false;
    }
    printf("[+] NvFBC64.dll loaded\n");

    m_pfnCreateEx       = (PFN_NvFBC_CreateEx)      GetProcAddress(m_hNvFBC, "NvFBC_CreateEx");
    m_pfnSetGlobalFlags = (PFN_NvFBC_SetGlobalFlags)GetProcAddress(m_hNvFBC, "NvFBC_SetGlobalFlags");
    m_pfnGetStatusEx    = (PFN_NvFBC_GetStatusEx)   GetProcAddress(m_hNvFBC, "NvFBC_GetStatusEx");
    m_pfnEnable         = (PFN_NvFBC_Enable)        GetProcAddress(m_hNvFBC, "NvFBC_Enable");
    if (!m_pfnCreateEx || !m_pfnSetGlobalFlags || !m_pfnGetStatusEx || !m_pfnEnable) {
        printf("[!] Failed to resolve NvFBC exports\n");
        return false;
    }

    // GetStatusEx + enable
    BYTE statusBuf[0x200] = {};
    NVFBC_STATUS* pStatus = (NVFBC_STATUS*)statusBuf;
    pStatus->dwVersion = 0x70020200;
    HRESULT hr = m_pfnGetStatusEx(pStatus);
    if (FAILED(hr)) { printf("[!] GetStatusEx failed: 0x%08X\n", hr); return false; }
    if (!(pStatus->dwFlags & 1)) {
        m_pfnSetGlobalFlags(6);
        hr = m_pfnEnable(1);
        if (FAILED(hr)) { printf("[!] Enable failed: 0x%08X\n", hr); return false; }
    }

    // Pre-CreateEx GetStatusEx
    memset(statusBuf, 0, sizeof(statusBuf));
    pStatus->dwVersion = 0x70020200;
    hr = m_pfnGetStatusEx(pStatus);
    if (FAILED(hr) || !(pStatus->dwFlags & 1) || !((pStatus->dwFlags >> 2) & 1)) {
        printf("[!] NvFBC not ready (flags=0x%08X)\n", pStatus->dwFlags);
        return false;
    }

    // CreateEx
    GUID captureGuid = { 0x0D7BC620, 0xE142, 0x4C17,
        { 0x97, 0x59, 0x6B, 0x5E, 0x5B, 0x85, 0x5A, 0x4B } };
    BYTE createBuf[0x200] = {};
    NVFBC_CREATE_PARAMS* pCreate = (NVFBC_CREATE_PARAMS*)createBuf;
    pCreate->dwVersion = 0x70020200;
    pCreate->dwMaxDisplayWidth = 4613;
    pCreate->pGuid = &captureGuid;
    pCreate->dwGuidSize = 16;
    hr = m_pfnCreateEx(pCreate);
    if (FAILED(hr) || !pCreate->qwHandle) {
        printf("[!] CreateEx failed: 0x%08X\n", hr);
        return false;
    }
    m_pInstance = (void*)pCreate->qwHandle;
    printf("[+] NvFBC instance: %p\n", m_pInstance);

    // CreateSession (vtable[0])
    BYTE sessionBuf[0x1F8] = {};
    NVFBC_SESSION_PARAMS* pSess = (NVFBC_SESSION_PARAMS*)sessionBuf;
    pSess->dwVersion = 0x700301F8;
    pSess->dwFlags = 0;
    pSess->dwFlags &= ~1u;
    pSess->dwFlags &= ~2u;
    pSess->pCaptureBuffer = &m_pCaptureBuffer;
    void** vt = (void**)*((void***)m_pInstance);
    typedef HRESULT (__fastcall* PFN_Sess)(void*, NVFBC_SESSION_PARAMS*);
    hr = ((PFN_Sess)vt[0])(m_pInstance, pSess);
    if (FAILED(hr)) { printf("[!] CreateSession failed: 0x%08X\n", hr); return false; }
    printf("[+] Session created, buffer: %p\n", m_pCaptureBuffer);

    m_bInitialized = true;
    return true;
}

bool NvFBC::GrabFrame(const BYTE*& outData, int& outWidth, int& outHeight) {
    if (!m_bInitialized) return false;
    int w = m_screenW, h = m_screenH;

    unsigned long stride  = 4 * (unsigned long)w;
    unsigned long bufSize = stride * (unsigned long)h;
    if (m_pixelsBuf.size() != bufSize)
        m_pixelsBuf.resize(bufSize);

    BYTE grabBuf[0x200] = {};
    NVFBC_GRAB_PARAMS* pGrab = (NVFBC_GRAB_PARAMS*)grabBuf;
    pGrab->dwVersion = 0x70010200;
    pGrab->dwWidth   = (DWORD)w;
    pGrab->dwHeight  = (DWORD)h;
    pGrab->dwFormat  = 2;
    pGrab->pOutputBuffer = m_pixelsBuf.data();

    void** vt = (void**)*((void***)m_pInstance);
    typedef HRESULT (__fastcall* PFN_Grab)(void*, NVFBC_GRAB_PARAMS*);
    HRESULT hr = ((PFN_Grab)vt[1])(m_pInstance, pGrab);
    if (FAILED(hr)) return false;

    if (m_pCaptureBuffer)
        std::memcpy(m_pixelsBuf.data(), m_pCaptureBuffer, bufSize);

    outData   = m_pixelsBuf.data();
    outWidth  = w;
    outHeight = h;
    return true;
}

void NvFBC::Cleanup() {
    if (m_pInstance) {
        void** vt = (void**)*((void***)m_pInstance);
        typedef HRESULT (__fastcall* PFN_Rel)(void*);
        ((PFN_Rel)vt[4])(m_pInstance);
        m_pInstance = nullptr;
    }
    if (m_hNvFBC)      { FreeLibrary(m_hNvFBC); m_hNvFBC = nullptr; }
    if (m_pD3DContext) { m_pD3DContext->Release(); m_pD3DContext = nullptr; }
    if (m_pD3DDevice)  { m_pD3DDevice->Release();  m_pD3DDevice = nullptr; }
    m_pCaptureBuffer = nullptr;
    m_bInitialized = false;
}
