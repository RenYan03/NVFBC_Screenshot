#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <vector>

/**
 * NvFBC (NVIDIA Frame Buffer Capture) — reverse-engineered from ACE_Shellcode sub_EC15
 *
 * Public API:
 *   Initialize()  -> set up D3D11 + NvFBC64.dll + create capture session
 *   GrabFrame()   -> capture one frame, returns BGRA pixels + dimensions
 *   Cleanup()     -> release all resources
 */

class NvFBC {
public:
    NvFBC();
    ~NvFBC();

    bool Initialize();

    // Capture one frame. Returns pointer to BGRA data (valid until next GrabFrame).
    bool GrabFrame(const BYTE*& outData, int& outWidth, int& outHeight);

    // Release all resources (called automatically by destructor).
    void Cleanup();

private:
    // ---- NVIDIA driver loading ----
    bool LoadNvidiaDrivers();
    bool IsNvidiaGPUAvailable();
    bool FindNvidiaDriverPath(char* outDir, size_t size);

    // ---- NvFBC structures (reverse-engineered from assembly) ----

    struct NVFBC_STATUS {
        DWORD dwVersion;
        DWORD dwFlags;          // bit0=created, bit2=can create
        DWORD dwPadding1;
        DWORD dwAdapterIdx;
    };

    struct NVFBC_CREATE_PARAMS {
        DWORD   dwVersion;
        DWORD   dwMaxDisplayWidth;
        DWORD   dwOutWidth;
        DWORD   dwOutHeight;
        DWORD64 qwExtra;
        GUID*   pGuid;
        DWORD   dwGuidSize;
        DWORD64 qwHandle;        // output: instance handle
        DWORD   dwAdapterIdx;
    };

    struct NVFBC_SESSION_PARAMS {
        DWORD   dwVersion;       // 0x700301F8
        DWORD   dwFlags;
        DWORD64 dwPadding1;
        DWORD64 dwPadding2;
        void*   pCaptureBuffer;  // filled by driver
        DWORD64 dwPadding3;
    };

    struct NVFBC_GRAB_PARAMS {
        DWORD   dwVersion;       // 0x70010200
        DWORD   dwFlags;
        DWORD   dwWidth, dwHeight;
        DWORD   dwSrcLeft, dwSrcTop;
        DWORD   dwFormat;
        DWORD   dwPadding;
        void*   pOutputBuffer;
    };

    typedef HRESULT (WINAPI* PFN_NvFBC_CreateEx)      (NVFBC_CREATE_PARAMS* pParams);
    typedef HRESULT (WINAPI* PFN_NvFBC_SetGlobalFlags) (DWORD dwFlags);
    typedef HRESULT (WINAPI* PFN_NvFBC_GetStatusEx)    (NVFBC_STATUS* pStatus);
    typedef HRESULT (WINAPI* PFN_NvFBC_Enable)         (DWORD dwEnable);


    // ---- members ----
    HMODULE m_hNvFBC;
    void*   m_pInstance;
    void*   m_pCaptureBuffer;
    ID3D11Device*        m_pD3DDevice;    // temp D3D11 device for driver loading
    ID3D11DeviceContext* m_pD3DContext;
    PFN_NvFBC_CreateEx       m_pfnCreateEx;
    PFN_NvFBC_SetGlobalFlags m_pfnSetGlobalFlags;
    PFN_NvFBC_GetStatusEx    m_pfnGetStatusEx;
    PFN_NvFBC_Enable         m_pfnEnable;
    bool m_bInitialized;
    int m_screenW = 0, m_screenH = 0;
    std::vector<BYTE> m_pixelsBuf;
};
