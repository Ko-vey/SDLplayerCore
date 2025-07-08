# SDLplayerCore

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL_v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0.html)

一个基于 C++11、FFmpeg7 和 SDL2 构建的轻量级音视频播放器核心。

![SDLplayerCore 播放器截图](docs/assets/pic1-program_run.png)

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
  - [计划功能](#计划功能)
  - [如何贡献](#如何贡献)
  - [许可证](#许可证)
  - [致谢](#致谢)
    - [核心依赖库](#核心依赖库)
    - [学习与参考资源](#学习与参考资源)


## 项目简介

`SDLplayerCore` 是一个基于现代 C++/FFmpeg/SDL 开发的实践型项目。项目实现了音视频播放中的缓存队列和流量控制设计、核心音视频同步和播放/暂停逻辑。开发者可以通过这个项目学习到：

- **FFmpeg API**: 如何结合 `libavformat` 库进行解复用，`libavcodec` 库进行音视频解码。

- **SDL2 API**: 如何创建窗口、渲染视频帧（AVFrame -> YUV -> RGB）、以及处理音频PCM数据。

- **多线程并发编程**: 如何通过独立且安全的线程分别管理数据读取与解复用、音视频解码和渲染模块，并通过现代C++的互斥锁和条件变量进行线程间通信。

- **音视频同步**: 基于 `SDL_QueueAudio` 实现一个基础但有效的音视频同步策略。

- **CMake 构建系统**: 配置一个依赖于外部库的、面向跨平台的项目。

## 架构概览 

本项目采用多线程“生产者-消费者”模型，将播放流程解耦为5个核心线程，它们之间通过线程安全的队列进行数据交换。

![核心模块与数据流图](docs/assets/pic2-basic_architecture.svg)

> 想要了解更详细的交互逻辑、时序关系和设计决策，请参阅
> [**详细设计文档**](docs/DESIGN.md)。

> 本项目的架构图和流程图使用 [Draw.io (diagrams.net)](https://app.diagrams.net/) 绘制。
>  图片文件 (`.svg`) 用于在文档中直接展示。对应的源文件 (`.drawio`) 存放在同一 `docs/assets/` 目录下。
> 如需修改图表，请编辑 `.drawio` 源文件并重新导出为 `.svg` 图片，然后将两个文件一并提交。

## 已有功能

- [x] 支持主流视频格式播放 (如 MP4, MKV, AVI, FLV 等)
- [x] 音画同步
- [x] 视频播放、暂停与停止
- [x] 窗口尺寸调整

## 快速开始

### 环境依赖

在编译运行程序前，确保开发环境满足以下要求：

- **操作系统**: Windows 10/11 (64-bit)。

- **IDE/编译器**: **Visual Studio 2022** (推荐 v17.9 或更高版本)，并确保已安装 “**使用 C++ 的桌面开发**” (Desktop development with C++) 工作负载。

- **CMake**: 版本 3.15 或更高。

- **Git**: 用于克隆本项目。

### 编译与构建

1. **克隆项目**
    ```bash
    git clone https://gitee.com/ko-vey/sdlplayer-core.git
    cd SDLplayerCore
    ```

2. **安装依赖项**

    本项目依赖于 FFmpeg7 和 SDL2。有两种方式来配置它们：

    **方式一：使用 vcpkg**

    [vcpkg](https://learn.microsoft.com/zh-cn/vcpkg/) 是一个 C++ 库管理器，可以简化依赖项的安装过程。

    ```bash
    # (如果尚未安装)安装 vcpkg ...
    git clone https://github.com/microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh # 或者 bootstrap-vcpkg.bat (Windows)

    # 为本项目安装依赖 (以 Windows x64 为例)
    # 确保是 FFmpeg7 和 SDL2 版本
    vcpkg install ffmpeg:x64-windows sdl2:x64-windows
    ```

    **方式二：手动下载与配置**

    如果希望手动管理依赖，请遵循以下步骤。

    1. **SDL2**
        - 从 [SDL2 官方发布页面](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.2) 下载 `SDL2-devel-2.32.2-VC.zip` (适用于 Visual C++)。
        - 在项目根目录下创建 `third_party/sdl2` 文件夹，并将解压后的内容组织如下：
        ```bash
        third_party/sdl2/
        ├── include/SDL2/  (存放所有 .h 文件)
        ├── lib/      (存放所有 .lib/.pdb 文件)
        └── bin/      (存放所有 .dll 文件)
        ```
    2. **FFmpeg7**
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
        # -DCMAKE_TOOLCHAIN_FILE 指向 vcpkg 的工具链文件
        cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg_path]/scripts/buildsystems/vcpkg.cmake

        # 编译
        cmake --build build
        ```

        - **如果手动配置依赖:**
        ```bash
        # 在 Developer Command Prompt for VS 2022 等终端中
        # 在项目根目录下
        cmake -S . -B build
        # 编译
        cmake --build build
        ```
        构建完成后，可执行文件将位于 `build/Release` 目录下。也可以使用 Visual Studio 打开 `build/SDLplayerCore.sln` 并进行编译和调试。

### 项目文件结构

在完成基本的准备工作后，项目文件结构大致如下：

```bash
.
├── CMakeLists.txt        # CMake 配置文件
├── README.md             # 本文档
├── src/                    # 源代码目录
│   ├── include/
│   │   ├── MediaPlayer.h
│   │   └── ...             # 其他头文件
│   └── source/
│       ├── MediaPlayer.cpp # 播放器核心逻辑类
│       ├── main.cpp        # 程序入口和主循环
│       └── ...             # 其他源文件
└── third_party/          # (可选) 手动存放第三方库
    ├── ffmpeg/
    └── sdl2/
```

## 如何使用

打开可执行文件`SDLPlayer.exe`后在命令行中输入目标视频文件的路径，启动播放器窗口：
```bash
Please enter the path to the video file and press Enter:
C:/path/to/your/video.mp4
```
> **运行时请确保 `FFmpeg` 和 `SDL2` 的 .dll 文件与可执行文件位于同一目录，或位于系统的 `PATH` 环境变量中。** 
> 如果是手动配置，可以将 `third_party` 目录下的 `bin` 文件夹内容复制到可执行文件(`.exe`)旁边。

- 使用 `空格键` 暂停/恢复播放
- 使用 `ESC键` 或者 `关闭播放器窗口` 停止播放
- 使用 `鼠标` 调整窗口大小，视频画面会进行自适应缩放

## 计划功能

- **基础控制**
  - [ ] 音量调节
  - [ ] 视频进度跳转 (Seek)
- **扩展功能**
  - [ ] 增加简单的 GUI 控件（如进度条、按钮）
  - [ ] 支持字幕文件加载与渲染 (如 `.srt`, `.ass`)
- **高级功能**
  - [ ] 倍速播放
  - [ ] 支持网络流媒体播放 (如 RTSP, HLS)
  - [ ] 更高级的音视频同步算法

## 如何贡献

非常欢迎并感谢任何形式的社区贡献！如果你想参与改进 `SDLplayerCore`，请遵循以下步骤：

1.  **沟通先行 (重要)**:
    - 如果你想修复一个 Bug，请先到 [Issues](https://gitee.com/ko-vey/sdlplayer-core/issues) 页面查看是否已有人提出。
    - 如果你计划增加一个新功能或进行重大修改，**建议先创建一个 Issue** 来讨论你的想法。这可以确保你的工作方向与项目规划一致，避免无用功。

2. **Fork** 本项目: 点击项目右上角的 "Fork" 按钮。随后克隆你的 Fork(`git clone https://gitee.com/YOUR-USERNAME/SDLplayerCore.git`)。

3. **创建功能分支**:
    - 一个好的分支名能清晰地表明意图，例如：
    ```bash
    # 修复Bug
    git checkout -b fix/memory-leak-on-close
    # 增加新功能
    git checkout -b feature/subtitle-rendering
    ```

4. **提交你的更改**:
   - 请尽量保持与项目现有代码一致的编码风格。
   - 进行代码修改，并在完成后提交。编写一个清晰的 Commit Message (重要)：
    ```bash
    # Commit Message 示例
    git commit -m "Fix: 修复播放器关闭时可能发生的内存泄漏问题"
    git commit -m "Feat: 添加 .srt 字幕解析与渲染功能"
    ```

5. **将分支推送到你的 Fork**:
    ```bash
    git push origin feature/subtitle-rendering
    ```

6. 创建一个 **Pull Request**:
    - 回到你的 Fork 页面，点击 "Contribute"，然后选择 "Open pull request"。
    - 在 PR 的描述中，请清晰地说明：
        - **此 PR 解决了什么问题？** (例如: "Fixes #123")
        - **做了哪些具体的改动？**
        - **你是如何测试的？** (如果有的话)

在收到 PR 后会尽快进行审查。感谢你的贡献！

## 许可证

本项目采用 **GNU 宽通用公共许可证 v3.0 (GNU Lesser General Public License v3.0)** 许可。完整的许可证文本请参见 [LICENSE](LICENSE) 文件。

**重要提示**:
- 本项目依赖于 **FFmpeg** (通常基于 LGPL) 和 **SDL2** (基于 zlib License)。
- 当您使用、修改或分发本软件时，必须同时遵守本项目许可证以及其所有依赖项的许可证规定。
- 根据 LGPL 的要求，您应当以**动态链接**的方式使用 FFmpeg 库，以允许用户替换该库。

## 致谢

这个项目得以实现，离不开以下优秀项目和无私分享的知识。在此向它们表示诚挚感谢。

### 核心依赖库

- [**FFmpeg**](https://ffmpeg.org/) - 强大的开源多媒体处理框架，是本项目中音视频解码、解复用等核心功能的基础。

- [**SDL2 (Simple DirectMedia Layer)**](https://libsdl.org/) - 优秀的跨平台多媒体开发库，承担了本项目中的窗口创建、视频渲染和音频播放工作。

### 学习与参考资源

- **雷霄骅（雷神）的音视频技术博客** - 向英年早逝的国内音视频技术领路人雷霄骅博士致敬。他的[《最简单的基于FFMPEG+SDL的视频播放器 ver2 （采用SDL2.0）》](https://blog.csdn.net/leixiaohua1020/article/details/38868499)及其系列文章是无数音视频开发者入门的启蒙教程，也是本项目的起点。

- [**ffplay.c 源码**](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c) - FFmpeg官方提供的播放器实现，它是学习音画同步、多线程处理等播放器核心逻辑的经典范例。

- [**FFmpeg开发入门教程 (知乎)**](https://zhuanlan.zhihu.com/p/682106665) - 一个出色的 C++ FFmpeg 开发入门教程，有助于初学者一步步理解基础的音视频概念并构建出基本demo。
