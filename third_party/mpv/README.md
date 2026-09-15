# third_party/mpv

本目录用于放置 **libmpv 开发包**，仓库中不包含二进制文件（`.dll` / `.dll.a` 已被 `.gitignore` 忽略），
克隆后需自行下载放入。

## 需要的文件

从 mpv 官方 Windows 构建（`mpv-dev-x86_64-*.7z`）解压后，本目录下应至少包含：

```
third_party/mpv/
├── include/mpv/*.h      # 头文件
├── libmpv.dll.a         # 导入库（CMake 用它静态导入）
└── libmpv-2.dll         # 运行时动态库（构建后会被复制到 exe 旁边）
```

## 获取方式

- 官方构建：https://sourceforge.net/projects/mpv-player-windows/files/libmpv/
- 或 `shinchiro/mpv-winbuild-cmake` 的 release 产物。

> 运行时需要的是与导入库**同一版本**的 `libmpv-2.dll`，混用不同构建可能加载失败。

## ffmpeg

封面、1080p 代理与预览片段依赖 `third_party/ffmpeg/ffmpeg.exe`。
未放置时 CMake 只给出警告，程序仍可构建，但转码相关功能不可用。
