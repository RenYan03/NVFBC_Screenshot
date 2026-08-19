# NVFBC_Screenshot

基于 NvFBC（NVIDIA Frame Buffer Capture）的 Windows 屏幕截图工具。直接调用 NVIDIA 驱动内置的 `NvFBC64.dll` 抓取全屏画面，通过 D3D11 实时预览，并叠加 ImGui 覆盖层显示 FPS，支持保存为 BMP / JPG / PNG。

## 功能

- 全屏实时捕获（BGRA 像素）
- D3D11 渲染预览 + ImGui 覆盖层（FPS / 分辨率 / 耗时）
- 保存为 BMP / JPG / PNG

## 构建

Visual Studio 2022（v143），x64，打开 `NVFBC_Screenshot.sln` 编译即可。

## 使用

运行 `NVFBC_Screenshot.exe`，预览窗口实时显示屏幕画面，点击覆盖层按钮保存截图（文件名 `screenshot_时间戳.ext`）。需要 NVIDIA 显卡及可用的 `NvFBC64.dll`。
