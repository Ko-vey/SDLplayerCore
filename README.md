<p align="right">
  <a href=".github/README.md"><strong>English</strong></a>
</p>

# SDLplayerCore

<p align="center">
  <img src="https://img.shields.io/badge/License-LGPL_v3-blue.svg" alt="License: LGPL v3">
  <img src="https://img.shields.io/badge/C%2B%2B-11-blue.svg" alt="C++11">
  <img src="https://img.shields.io/badge/FFmpeg-7.0-brightgreen.svg" alt="FFmpeg 7.0">
  <img src="https://img.shields.io/badge/SDL-2.0-orange.svg" alt="SDL2">
  <img src="https://img.shields.io/badge/Platform-Windows-informational.svg" alt="Platform: Windows">
  </p>

**一款基于 C++、FFmpeg 和 SDL 打造的轻量级音视频流媒体播放器，助你深入理解 C++、FFmpeg 与 SDL 的音视频应用。**

![SDLplayerCore 运行界面截图](docs/assets/screenshot-program-run.png)

## 目录

- [SDLplayerCore](#sdlplayercore)
  - [目录](#目录)
  - [项目简介](#项目简介)
  - [架构概览](#架构概览)
  - [已有功能](#已有功能)
  - [快速开始](#快速开始)
    - [环境依赖](#环境依赖)
    - [编译与构建](#编译与构建)
    - [项目文件结构](#项目文件结构)
  - [如何使用](#如何使用)
  - [问题反馈](#问题反馈)
  - [许可证](#许可证)
  - [致谢](#致谢)
    - [核心依赖库](#核心依赖库)
    - [参考资源](#参考资源)
    - [生产力工具](#生产力工具)


## 项目简介

`SDLplayerCore` 是一款基于 C++11、FFmpeg7 和 SDL2 开发的、主要面向 Windows 平台的轻量级音视频播放器。它既可以播放本地媒体文件，也可以播放流媒体（RTSP、RTMP）。

> *注意：本项目的主要目标是教学和演示，而不是打造一个功能完备、可以代替 VLC 或者 PotPlayer 等成熟商业级产品的日常播放器。*

经过 Visual Studio 性能探查器分析，`SDLplayerCore` 在 Windows 11、锐龙 R7-5800H 处理器下播放 1080P@30fps H.264 视频时，CPU 占用率稳定在 3% 以下，展示了其高效的性能。

该播放器项目目前实现了以下几个核心功能:

- 数据管线 - **缓存队列** 和 **流量控制** 设计
- 播放器核心 - **音视频同步**
- 事件响应/状态管理 - **窗口调整**、**播放/暂停**等
- 流媒体支持 - *RTSP (TCP / UDP)*、*RTMP*
- 调试信息层 - 时钟源、播放器状态、FPS 等

开发者可以通过这个项目学习到：

- **FFmpeg7 API**: 结合 `libavformat` 库进行解复用，`libavcodec` 库进行音视频解码。
- **SDL2 API**: 如何创建窗口、渲染视频帧（AVFrame -> YUV -> RGB）、以及处理音频PCM数据。
- **多线程并发编程**: 通过独立且安全的线程分别管理数据读取与解复用、音视频解码和渲染模块，并通过现代 C++ 的互斥锁和条件变量进行线程间通信。
- **音视频同步**: 基于 `SDL_QueueAudio` 实现一个基础但有效的音视频同步策略。
- **CMake 构建系统**: 配置一个依赖于外部库的、面向跨平台的项目。

**动态演示：**

下图展示了播放器播放视频、窗口拉伸、暂停/恢复的功能。

![SDLplayerCore 动态展示](docs/assets/demo-program-run.gif)

## 架构概览

本播放器项目采用了经典的多线程“生产者-消费者”模型架构，将播放流程解耦为1个主线程、5个工作子线程、1个控制子线程，它们之间通过线程安全的缓存队列进行数据交换。

> **设计细节**
> 如果你想要深入了解本播放器的技术细节（如音画同步等），
> 请参阅 **[设计文档 (DESIGN.md)](docs/DESIGN.md)**。

`SDLplayerCore` 的基本架构和数据流示意图如下：

![数据流与基本架构](docs/assets/flow-basic-architecture.svg)

在本项目中，我们通过有限状态机来组织状态流转，具体模式如下：

![状态流转模式](docs/assets/finite_state_machine.svg)

> **关于图表**
> <details>
>   <summary>点击查看图表源文件与编辑说明</summary>
> 
> 本项目中的架构图和流程图使用 [Mermaid](https://mermaid.js.org/) 和 [Draw.io](https://www.drawio.com/) 绘制。
> 
> 文档内直接展示的插图为 `.svg` 格式，其对应源文件 (`.drawio` 文件以及部分以 `.md` 格式存储的 `mermaid` 源码) 存放在 `docs/assets/` 目录下。
> 
> 如需修改图表，推荐流程：优先编辑 `.md` 文件中的 `mermaid` 源代码，将其导入对应的 `.drawio` 源文件进行调整，然后导出为新的 `.svg` 图片，并更新文档中的图片路径，最后将所有相关文件一并提交。
> </details>

## 已有功能

- [x] 播放主流视频/音频格式 (如 MP4, AVI, FLV, MP3, WAV, FLAC 等)
- [x] 音画同步
- [x] 播放、暂停与终止
- [x] 窗口调整
- [x] 播放流媒体 (RTSP, RTMP)
- [x] 调试信息层

## 快速开始

### 环境依赖

在编译运行程序前，确保开发环境满足以下要求：
- **操作系统**: Windows 10/11 (64-bit)
- **IDE/编译器**: Visual Studio 2022 (v17.9+)，并已安装 “使用 C++ 的桌面开发” 工作负载。
- **构建系统**: CMake(3.15+)
- **版本控制**: Git

### 编译与构建

1. **克隆项目**
    ```bash
    git clone https://gitee.com/ko-vey/sdlplayer-core.git
    cd SDLplayerCore
    ```

2. **安装依赖项**

    本项目依赖于 **FFmpeg7** 和 **SDL2**。

    **方式一：使用 vcpkg**

    [vcpkg](https://learn.microsoft.com/zh-cn/vcpkg/) 是一个高效的 C++ 库管理器。

    ```bash
    # (如果尚未安装)安装 vcpkg ...
    git clone https://github.com/microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh # 或者 bootstrap-vcpkg.bat (Windows)

    # 为本项目安装依赖 (以 Windows x64 为例)
    # 确保是 FFmpeg7 和 SDL2 版本
    vcpkg install ffmpeg:x64-windows sdl2:x64-windows
    ```

    **方式二：手动下载与配置**

    如果希望手动管理依赖：

    1. **SDL2**
        - 从 [SDL2 官方发布页面](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.2) 下载 `SDL2-devel-2.32.2-VC.zip` (适用于 Visual C++)。
        - 从 [SDL_ttf 发布页面](https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/) 下载 `SDL2_ttf-devel-2.24.0-VC.zip`。
        - 在项目根目录下创建 `third_party/sdl2` 文件夹，并将解压后的内容组织如下：
          ```bash
          third_party/sdl2/
          ├── include/SDL2/  (存放所有 .h 文件)
          ├── lib/      (存放所有 .lib/.pdb 文件)
          └── bin/      (存放所有 .dll 文件)
          ```
        > 注意: 调试信息层需要 `SDL_ttf` 插件来显示字体。请确保你的系统中有 `C:/Windows/Fonts/arial.ttf` 这个字体文件（可在`src/source/SDLVideoRenderer.cpp` 中调整。本项目也提供备用文件 `third_party/fonts/arial.ttf`）。

    2. **FFmpeg 7**
        - 从 [FFmpeg 官网推荐的 Windows Builds](https://github.com/ShiftMediaProject/FFmpeg/releases/tag/7.0) 下载 shared 版本的库 (`libffmpeg_7.0_msvc17_x64.zip`)。
        - 在项目根目录下创建 `third_party/ffmpeg` 文件夹，并将解压后的内容组织如下：
          ```bash
          third_party/ffmpeg/
          ├── include/  (存放 libav* 等头文件)
          ├── lib/      (存放 .lib 文件)
          └── bin/      (存放 .dll 文件)
          ```
        > 注意: 手动配置时，请确保 `CMakeLists.txt` 中的 find_package 路径与目录结构相匹配。

    3. **构建项目**
        - **如果使用 vcpkg:**
          ```bash
          cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg_path]/scripts/buildsystems/vcpkg.cmake
          cmake --build build
          ```

        - **如果手动配置依赖:**
          ```bash
          # 在项目根目录下
          cmake -S . -B build
          cmake --build build
          ```

    构建完成后，可执行文件将位于 `build/Release` 目录下。也可以使用 Visual Studio 打开 `build/SDLplayerCore.sln` 并进行编译和调试。

### 项目文件结构

```bash
SDLplayerCore/
├── .github/              # Github 平台的相关配置
├── CHANGELOG.md          # 更新日志
├── CMakeLists.txt        # CMake 配置文件，定义项目和依赖项
├── LICENSE               # 许可证文件
├── README.md             # 项目指南
├── build/                # CMake 构建目录，存放中间文件和最终产物
├── docs/                 # 项目文档
│   ├── assets/           # 图片源文件等
│   └── DESIGN.md         # 设计细节
├── src/                  # 项目源代码
│   ├── include/          # 头文件 (.h)
│   │   ├── MediaPlayer.h # 播放器主类定义
│   │   └── ...
│   └── source/           # 源文件 (.cpp)
│       ├── MediaPlayer.cpp # 播放器主类实现
│       ├── main.cpp      # 程序入口，处理用户输入和窗口事件
│       └── ... 
└── third_party/          # (可选) 用于手动存放第三方依赖库
    ├── ffmpeg/
    └── sdl2/
```

## 如何使用

1. **直接运行**:
   - 打开命令行工具 (如 CMD 或 PowerShell)，进入该目录，运行可执行程序（.exe）。
   
     > **提示**：CMake 配置已包含自动复制依赖脚本。
     > 如果遇到缺少 DLL 的报错，请手动将 `third_party/.../bin/` 下的 `.dll` 文件复制到 `SDLPlayer.exe` 同级目录。

2. **提供媒体文件**:

    程序启动后会提示输入媒体路径或URL。在终端窗口输入媒体文件的完整路径，或者直接将文件拖拽到终端窗口中，然后按回车键。

    ```powershell
    # 示例
    PS D:\path\to\SDLplayerCore\build\Release> ./SDLPlayer.exe

    Please enter the path to the media file and press Enter:
    D:\Videos\demo.mp4
    ```

    > **关于测试文件**
    > <details>
    >   <summary>点击这里获取无版权的标准测试资源</summary>
    >
    > - **音频 + 视频：** [Big Buck Bunny](https://peach.blender.org/download/)
    > - **纯视频：** [Jellyfish Bitrate Test Files](https://repo.jellyfin.org/archive/jellyfish/)
    > - **纯音频：** [Sample MP3](https://file-examples.com/index.php/sample-audio-files/sample-mp3-download/)
    >
    > </details>

3. **播放控制**:
   - **播放/暂停**: `空格键`。
   - **停止播放**: `ESC键` 或者 `关闭播放器窗口` 。
   - **调整窗口**: 使用 `鼠标` 拖动窗口边缘。

## 问题反馈

本项目主要作为个人的开发记录与技术展示。因此，目前不主动寻求代码贡献（PR, Pull Requests）。

但是，欢迎任何形式的**交流与反馈**！如果你在使用中遇到任何问题和 Bug，或者有任何建议和想法，请通过 **创建 Issue** 联系：在本项目的 [Issues 页面](https://gitee.com/ko-vey/sdlplayer-core/issues) 提交。我会定期查看并回复。

感谢你的关注与理解！

## 许可证

本项目采用 **LGPLv3 (GNU Lesser General Public License v3.0)** 许可。完整的许可证文本请参见 [LICENSE](LICENSE) 文件。

**重要提示**:

- 本项目依赖于 **FFmpeg** (基于 LGPL) 和 **SDL2** (基于 zlib License)。
- 当您使用、修改或分发本软件时，必须同时遵守本项目许可证以及其所有依赖项的许可证规定。
- 根据 LGPL 的要求，您应当以**动态链接**的方式使用 FFmpeg 库，以允许用户替换该库。

## 致谢

在此向以下项目、工具和无私分享的知识表示诚挚感谢。

### 核心依赖库

- [**FFmpeg**](https://ffmpeg.org/) - 强大的开源多媒体处理框架，是本项目音视频解码、解复用等核心功能的基础。

- [**SDL2 (Simple DirectMedia Layer)**](https://libsdl.org/) - 优秀的跨平台多媒体开发库，用于本项目中的窗口创建、视频渲染和音频播放等工作。

### 参考资源

- **雷霄骅的技术博客** - 在此向英年早逝的国内音视频技术领路人雷霄骅博士表示崇高致敬。他的[《最简单的基于FFMPEG+SDL的视频播放器》](https://blog.csdn.net/leixiaohua1020/article/details/38868499)系列文章是国内无数音视频开发者入门的启蒙教程，也是本项目的起点。

- [**ffplay.c**](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c) - FFmpeg官方提供的桌面端播放器，是学习音画同步、多线程处理等播放器核心逻辑的最权威且最经典的范例。

- [**ijkplayer**](https://github.com/bilibili/ijkplayer) - bilibili团队提出的移动端播放器，是移动端播放器领域的最佳实践和标准答案。

- [**FFmpeg开发入门教程 (知乎)**](https://zhuanlan.zhihu.com/p/682106665) - 优秀的 C++ FFmpeg 教程，能帮助初学者快速构建第一个demo。

### 生产力工具

- **大型语言模型 (LLM)** - 在本项目的信息检索、方案设计、代码实现、文档撰写等多个环节中提供了无数帮助，提升了本项目的开发效率。
