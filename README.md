# Clips Workbench

面向素材量很大的剪辑团队：扫描本地磁盘或共享盘上的视频，打标签分类检索，多台电脑共用同一个工作区。

A desktop application for managing large video asset libraries. It scans video
files on local disks or SMB shares, organizes them with tags, and lets several
machines share one workspace.

<img width="1361" height="897" alt="8dc2911c1012be58bfa4ee772d0bba5a" src="https://github.com/user-attachments/assets/2cb004a1-eec7-4e82-b543-8732f6093939" />

---

## 简体中文

### 功能

- **素材入库**：指定一个工作区文件夹，程序扫描里面的视频，把时长、分辨率、编码写进 SQLite 索引。
- **标签**：镜头内容、产品、可用性三组独立标签，可以多选，也支持「且 / 或」组合筛选。
- **筛选与排序**：左侧栏按调用次数（≥ N 次）和有无标签过滤；「上传时间」按的是素材文件的修改时间。
- **预览**：卡片上直接低清预览，点开是内置的 libmpv 播放器，支持全屏，也可以丢给外部播放器。
- **自动生成**：封面、1080p 代理和预览片段由 ffmpeg 生成。
- **多人协作**：每 15 秒探一次库版本，别人在别的电脑改了标签会自己刷新；新文件要手动点扫描入库。
- **登录 / 工作区**：本地文件夹直接填路径，共享盘（`\\服务器\共享名\…`）再填账号密码，可以记住账号。
- **删除**：本地素材进回收站；共享盘上的直接删除、找不回来（确认框里会写明）。

### 技术栈

| 组件 | 用途 |
| --- | --- |
| Qt 6.11（Core / Gui / Quick / QuickControls2 / Sql / Multimedia / OpenGL） | 界面与运行时 |
| libmpv | 视频播放内核，走 OpenGL 直接绘制进 Qt 场景图 |
| ffmpeg | 封面 / 代理转码 / 预览片段生成 |
| SQLite（Qt Sql） | 素材索引库 |

界面全部由 QML 自绘，无边框窗口 + 圆角 + 毛玻璃，控件样式用 `QQuickStyle::setStyle("Basic")` 打底。

### 目录结构

```
.
├── CMakeLists.txt
├── build.cmd                 # 一键构建，Qt 路径按自己机器改
├── qml/                      # 所有界面文件
│   └── Theme.qml             # 颜色和尺寸的单例，改主题看这里
├── src/
│   ├── main.cpp              # 入口：设置渲染后端、处理窗口圆角
│   ├── core/
│   │   ├── AppBridge.*       # 登录、切工作区（QML 里叫 App）
│   │   ├── Config.*          # 读写 config.json
│   │   ├── Db.*              # SQLite 连接和建表
│   │   ├── LibraryService.*  # 查素材、改标签、出封面（QML 里叫 Library）
│   │   ├── MediaMeta.*       # 读视频时长、分辨率、编码
│   │   ├── ProxyService.*    # 生成 1080p 代理和预览片段
│   │   └── Scanner.*         # 扫文件夹、写索引
│   └── players/
│       └── MpvItem.*         # mpv 播放，画面直接画进 Qt 场景图
└── third_party/
    └── mpv/                  # libmpv 开发包放这里，不进仓库
```

### 构建

**依赖**

- **Qt 6.5+**（开发时使用 6.11.2 / MinGW 13.1 64 位），需要
  `Quick`、`QuickControls2`、`Sql`、`Multimedia`、`OpenGL` 模块
- **CMake 3.21+**、**Ninja**（或别的生成器）、**MinGW 13.1**（或 MSVC）
- **libmpv 开发包**：放进 `third_party/mpv/`，要哪些文件见
  [`third_party/mpv/README.md`](third_party/mpv/README.md)
- **ffmpeg.exe**（可选）：放进 `third_party/ffmpeg/`，没有的话转码类功能用不了

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt 安装路径>/6.11.2/mingw_64 \
      -DWB_CONSOLE=OFF
cmake --build build
```

生成的可执行文件在 `build/workbench_qt.exe`，`libmpv-2.dll` 会在构建后自动复制到它旁边。

**CMake 选项**

- `WB_CONSOLE`（默认 `ON`）：带一个控制台窗口，方便看 `qDebug` 和 QML 报错；打包时设成 `OFF`。
- `WB_MPV_DIR`：libmpv 开发包目录，默认 `third_party/mpv`。
- `WB_FFMPEG_DIR`：ffmpeg 目录，默认 `third_party/ffmpeg`。

### 配置与数据

- **配置**：`%APPDATA%\ClipsWorkbench\config.json`，不在程序目录里，移动程序不会丢。
- **数据**：素材索引库、封面、1080p 代理、预览片段都在所选「工作区文件夹」下的
  `工作台数据库` 目录里，换电脑时把工作区文件夹一起带走即可。

### 说明

- 程序没有数字签名，杀毒软件可能拦截，选择「允许 / 信任」即可。
- 缩略图或播放不正常时，先更新显卡驱动：视频画面走 OpenGL 渲染。

---

## English

### Features

- **Library scanning** — point it at a workspace folder and it indexes every video it finds: duration, resolution and codec, all into SQLite.
- **Tags** — three separate groups (shot content, product, availability); multi-select, with AND / OR matching.
- **Filtering & sorting** — the side bar filters by call count (≥ N) or by whether a clip has tags; "upload time" uses the file's modified time.
- **Preview** — low-res preview right on the card, or open the built-in libmpv player (fullscreen, or hand it to an external player).
- **Generated media** — thumbnails, 1080p proxies and preview clips, all produced by ffmpeg.
- **Multi-user** — it polls the library version every 15 seconds, so tag edits from other machines show up on their own; new files still need a manual scan.
- **Login / workspace** — a local folder is just a path; a share (`\\server\share\…`) also wants an account and password, which can be remembered.
- **Deleting** — local files go to the Recycle Bin; files on a share are gone for good (the dialog says so).

### Tech stack

| Component | Role |
| --- | --- |
| Qt 6.11 (Core / Gui / Quick / QuickControls2 / Sql / Multimedia / OpenGL) | UI and runtime |
| libmpv | Playback core, rendered with OpenGL directly into the Qt scene graph |
| ffmpeg | Thumbnails, proxy transcoding, preview clips |
| SQLite (Qt Sql) | Video index database |

The whole UI is hand-drawn in QML: frameless window, rounded corners, frosted
glass, with `QQuickStyle::setStyle("Basic")` as the base style.

### Project layout

```
.
├── CMakeLists.txt
├── build.cmd                 # one-shot build; adjust the Qt paths for your machine
├── qml/                      # all UI files
│   └── Theme.qml             # singleton for colors and sizes
├── src/
│   ├── main.cpp              # entry point: renderer backend, window rounding
│   ├── core/
│   │   ├── AppBridge.*       # login and workspace switching (App in QML)
│   │   ├── Config.*          # reads and writes config.json
│   │   ├── Db.*              # SQLite connection and schema
│   │   ├── LibraryService.*  # clip queries, tags, thumbnails (Library in QML)
│   │   ├── MediaMeta.*       # duration, resolution, codec
│   │   ├── ProxyService.*    # 1080p proxies and preview clips
│   │   └── Scanner.*         # walks folders and fills the index
│   └── players/
│       └── MpvItem.*         # mpv playback, drawn into the Qt scene graph
└── third_party/
    └── mpv/                  # libmpv dev package lives here, not in the repo
```

### Building

**Requirements**

- **Qt 6.5+** (developed against 6.11.2 / MinGW 13.1 64-bit) with the
  `Quick`, `QuickControls2`, `Sql`, `Multimedia` and `OpenGL` modules
- **CMake 3.21+**, **Ninja** (or another generator), **MinGW 13.1** (or MSVC)
- **libmpv dev package** placed in `third_party/mpv/`; see
  [`third_party/mpv/README.md`](third_party/mpv/README.md) for the files needed
- **ffmpeg.exe** (optional) in `third_party/ffmpeg/`; without it the transcoding
  features are unavailable

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt path>/6.11.2/mingw_64 \
      -DWB_CONSOLE=OFF
cmake --build build
```

The executable lands at `build/workbench_qt.exe`, and `libmpv-2.dll` is copied
next to it after the build.

**CMake options**

- `WB_CONSOLE` (default `ON`) — attaches a console window for `qDebug` and QML
  errors; set it to `OFF` when packaging.
- `WB_MPV_DIR` — libmpv dev package directory, defaults to `third_party/mpv`.
- `WB_FFMPEG_DIR` — ffmpeg directory, defaults to `third_party/ffmpeg`.

### Configuration and data

- **Config**: `%APPDATA%\ClipsWorkbench\config.json`, outside the program folder,
  so moving the program keeps your settings.
- **Data**: the video index, thumbnails, 1080p proxies and preview clips live in a
  `工作台数据库` folder inside the workspace folder you pick; take that folder with
  you when you switch machines.

### Notes

- The program is not code-signed, so antivirus software may flag it; allow it.
- If thumbnails or playback misbehave, update your graphics driver first: video is
  rendered through OpenGL.

---

## License

[MIT](LICENSE)
