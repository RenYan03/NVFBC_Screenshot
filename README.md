# NVFBC_Screenshot

基于 **NvFBC（NVIDIA Frame Buffer Capture）** 的 Windows 屏幕截图查看器。通过直接调用 NVIDIA 驱动的帧缓冲捕获接口实时抓取全屏画面，使用 D3D11 全屏四边形渲染，并叠加 ImGui 覆盖层显示 FPS 与抓取耗时，支持一键保存为 BMP / JPG / PNG。

## 功能特性

- **全屏实时捕获**：调用 `NvFBC64.dll` 底层接口抓取整块屏幕帧缓冲，输出 BGRA 像素
- **D3D11 渲染预览**：内嵌 HLSL 着色器，以全屏三角形条带（triangle strip）无顶点缓冲渲染抓取画面
- **ImGui 覆盖层**：实时显示 FPS、分辨率、抓取/上传耗时（毫秒）
- **多格式保存**：
  - **BMP**：直接写原始 BGRA 数据（32 位、自上而下）
  - **JPG**：经 WIC 编码，100% 质量
  - **PNG**：经 WIC 编码，自动修复 NvFBC 输出中 Alpha=0 的问题
## 技术实现

### NvFBC 接口调用（逆向工程）

`NvFBC64.dll` 是 NVIDIA 未公开文档的内部捕获库。本项目基于某反作弊逆向得出结果，通过结构体猜测还原了其核心调用流程：

| 阶段 | 函数 / 接口 | 说明 |
|------|------------|------|
| 加载驱动 | `LoadNvidiaDrivers()` | 枚举 DXGI 适配器找到 VendorId `0x10DE` 的 NVIDIA 显卡 |
| 加载库 | `LoadLibrary("NvFBC64.dll")` | 从驱动目录或 `System32` 加载 |
| 初始化 | `NvFBC_CreateEx` / `NvFBC_Enable` / `NvFBC_SetGlobalFlags` | 创建捕获实例 |
| 建会话 | 实例虚表 `vtable[0]` | 创建捕获会话，由驱动回填捕获缓冲区 |
| 抓帧 | 实例虚表 `vtable[1]` | 抓取一帧到本地缓冲区 |
| 释放 | 实例虚表 `vtable[4]` | 释放实例 |

关键逆向结构体见 [NvFBC.h](NVFBC_Screenshot/NvFBC.h)：`NVFBC_CREATE_PARAMS`、`NVFBC_SESSION_PARAMS`、`NVFBC_GRAB_PARAMS` 等。

## 构建

- **工具链**：Visual Studio 2022（`v143` 工具集）
- **平台**：x64 / Win32（Debug、Release）
- **Windows SDK**：10.0

直接打开 `NVFBC_Screenshot.sln` 编译即可。已链接依赖库：`d3d11`、`d3dcompiler`、`windowscodecs`、`dxgi`、`gdi32`、`user32`。

## 使用

1. 运行生成的 `NVFBC_Screenshot.exe`（控制台程序，附带 GUI 预览窗口）
2. 预览窗口实时显示屏幕画面与左上角覆盖层
3. 点击覆盖层按钮保存截图，文件以 `screenshot_YYYYMMDD_HHMMSS.ext` 命名写入工作目录

> 需要 NVIDIA 显卡及可用的 `NvFBC64.dll`（现代驱动通常内置）。

## 目录结构

```
NVFBC_Screenshot/
├── NVFBC_Screenshot.sln              # Visual Studio 解决方案
├── NVFBC_Screenshot/
│   ├── NvFBC.h / NvFBC.cpp           # NvFBC 接口封装 + 显示亲和性 Hook
│   ├── NVFBC_Screenshot.cpp          # 入口：D3D11 渲染、ImGui、图像保存
│   └── imgui/                        # 内置 ImGui 源码
└── README.md
```

## 免责声明

本项目仅供学习与研究 NVIDIA NvFBC 接口逆向、D3D11 渲染与 Windows Hook 技术之用。请遵守当地法律法规，勿将其用于侵犯他人权益的场景。
