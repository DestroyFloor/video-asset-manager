cmake_minimum_required(VERSION 3.21)

project(workbench_qt VERSION 0.1.0 LANGUAGES CXX)

# 与旧 Electron 版共用同一套数据，但不共用任何代码/框架。
# 旧版目录 D:\python_file\CB_web\ 不变，本工程完全独立。
#
# 构建：直接运行同目录的 build.cmd（Ninja + MinGW 13.1 + Qt 6.11.2），
# 或用 Qt Creator 打开本文件。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WB_CONSOLE "Build with a console window attached (development)" ON)

# Multimedia 用于卡片上的「原地预览」（低清短片段）
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Sql QuickDialogs2QuickImpl OpenGL Multimedia)
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------- libmpv
# 通过 third_party/mpv 里的官方 dev 包静态导入（libmpv.dll.a）动态加载 libmpv-2.dll。
set(WB_MPV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv" CACHE PATH "libmpv dev package directory")

if(NOT EXISTS "${WB_MPV_DIR}/libmpv.dll.a")
    message(FATAL_ERROR
        "找不到 libmpv 导入库：${WB_MPV_DIR}/libmpv.dll.a\n"
        "请把 mpv-dev-x86_64-*.7z 解压到 third_party/mpv/（需要 include/mpv/*.h、libmpv.dll.a、libmpv-2.dll）。")
endif()

add_library(mpv SHARED IMPORTED GLOBAL)
set_target_properties(mpv PROPERTIES
    IMPORTED_LOCATION "${WB_MPV_DIR}/libmpv-2.dll"
    IMPORTED_IMPLIB   "${WB_MPV_DIR}/libmpv.dll.a"
    INTERFACE_INCLUDE_DIRECTORIES "${WB_MPV_DIR}/include"
)

# ---------------------------------------------------------------- ffmpeg
# 供 ProxyService 生成 1080p 代理、LibraryService 生成封面与预览片段用。
set(WB_FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "ffmpeg directory")

if(NOT EXISTS "${WB_FFMPEG_DIR}/ffmpeg.exe")
    message(WARNING
        "未找到 ${WB_FFMPEG_DIR}/ffmpeg.exe —— 依赖 ffmpeg 的功能（代理转码/封面/预览）将不可用。")
endif()

# ---------------------------------------------------------------- app
qt_add_executable(workbench_qt
    src/main.cpp
    src/core/Config.h      src/core/Config.cpp
    src/core/Db.h          src/core/Db.cpp
    src/core/MediaMeta.h   src/core/MediaMeta.cpp
)

# Theme.qml 是 QML 单例：显式声明该属性，确保生成的 qmldir 带 singleton 前缀，
# 否则 QML 里读 Theme.xxx 会拿到 undefined（颜色也会退化成默认值）。
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# 暴露给 QML 的 C++ 类型：
#   AppBridge → App、LibraryService → Library、ProxyService → Proxy、
#   Scanner → Scanner、MpvItem → MpvVideo
qt_add_qml_module(workbench_qt
    URI Workbench
    VERSION 1.0
    SOURCES
        src/core/AppBridge.h       src/core/AppBridge.cpp
        src/core/LibraryService.h  src/core/LibraryService.cpp
        src/core/ProxyService.h    src/core/ProxyService.cpp
        src/core/Scanner.h         src/core/Scanner.cpp
        src/players/MpvItem.h      src/players/MpvItem.cpp
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/LoginView.qml
        qml/LibraryView.qml
        qml/FilterPane.qml
        qml/GridPane.qml
        qml/DetailPane.qml
        qml/TagDialog.qml
        qml/PlayerOverlay.qml
        # 共享控件（同目录 = 同一 QML 模块，其它 qml 里可直接当类型用）
        qml/Btn.qml
        qml/IconBtn.qml
        qml/Dropdown.qml
        qml/Toggle.qml
)

target_link_libraries(workbench_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::QuickDialogs2QuickImpl
    Qt6::OpenGL
    Qt6::Multimedia
    mpv
)

# 生成的 qmltyperegistrations.cpp 用 #include <AppBridge.h> 这类写法引用暴露给 QML 的类型，
# 所以这些目录都必须在 include 路径中。
target_include_directories(workbench_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/players"
)

# 开发期带控制台窗口，便于看到 qDebug / QML 报错；打包时用 -DWB_CONSOLE=OFF 关掉。
if(WB_CONSOLE)
    set_target_properties(workbench_qt PROPERTIES WIN32_EXECUTABLE FALSE)
else()
    set_target_properties(workbench_qt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# libmpv 运行时 DLL 放到可执行文件旁边
add_custom_command(TARGET workbench_qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WB_MPV_DIR}/libmpv-2.dll"
            "$<TARGET_FILE_DIR:workbench_qt>"
    COMMENT "复制 libmpv-2.dll 到输出目录")
