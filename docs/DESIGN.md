# SDLplayerCore 详细设计文档

本文档旨在深入阐述 SDLplayerCore 的内部架构、线程模型和核心功能的实现细节。

## 1. 总体架构与数据流

本项目采取了类似 [`ffplay`](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c) 的经典"生产者-消费者"架构，通过多线程实现模块间的解耦与并行处理。播放器的核心模块与数据流示意图如下:

![核心模块与数据流图](assets/pic2-basic_architecture.svg)

每个模块都有各自明确的任务:

1. **解复用模块 (Demuxer)**: 负责读取媒体文件（如 MP4, MKV），解析其容器格式，并将分离出的音频、视频压缩数据包 (`AVPacket`、`AVFrame`) 分别送入对应的缓存队列中。

2. **缓存队列模块 (Queue)**: 作为线程间数据交换的缓冲池，用于存放 `AVPacket` 和 `AVFrame`。它解决了生产者与消费者速率不匹配的问题，并提供了必要的线程安全与同步机制。本项目中的 `PacketQueue` (数据包队列) 和 `FrameQueue` (数据帧队列) 在设计上基本一致。

3. **视频解码模块 (Video Decoder)**: 从视频包队列 (`VideoPacketQueue`) 中获取 `AVPacket`，利用 FFmpeg 解码器将其解码为原始视频数据帧 (`AVFrame`)，然后将 `AVFrame` 放入视频帧队列 (`VideoFrameQueue`)。

4. **视频渲染模块 (Video Renderer)**: 从视频帧队列 (`VideoFrameQueue`) 中获取 `AVFrame`，根据同步时钟决定最佳渲染时机。在渲染前，需要通过 `sws_scale` 进行色彩空间转换（从 `AVFrame` 到 YUV）。最终使用 SDL2 的渲染函数将图像绘制到窗口上。

5. **音频解码模块(Audio Decoder)**: 从音频包队列 (`AudioPacketQueue`) 中获取 `AVPacket`，利用 FFmpeg 解码器将其解码为原始音频数据帧 (`AVFrame`)，然后将 `AVFrame` 放入音频帧队列 (`AudioFrameQueue`)。

6. **音频渲染模块 (Audio Renderer)**: 从音频帧队列 (`AudioFrameQueue`) 中获取 `AVFrame`。如果解码后的音频格式（如采样率、声道数）与设备不符，会通过 `swr_convert` 进行重采样。最终，将处理好的 PCM 数据推送给 SDL 的音频缓冲区进行播放。

7. **同步时钟模块 (Clock Manager)**: 负责统一管理播放时间。通常以音频时钟为主时钟（因为人耳对音频的延迟更敏感），当无法获取音频时使用外部时钟（SDL2提供的系统时钟）；视频通过比对自身的显示时间戳 (PTS) 与主时钟来调整播放节奏（延迟、跳帧或加速），从而实现音视频同步。

这些模块被集成在主类 `MediaPlayer` 中，通过初始化辅助函数 `MediaPlayer::start_threads()` 和 主循环函数 `MediaPlayer::runMainLoop` 启动各个工作线程，并通过定时轮询的事件处理机制 `MediaPlayer::handle_event` 来响应用户的交互操作。

更细节具体的模块交互时序图如下:

![核心模块交互时序图](assets/pic3-detailed_core_architecture.svg)

在各个线程中，每个模块都通过对应的接口执行相应关键任务:

**1.  解复用线程 (Demux Thread)**: 

- 通过 `avformat_open_input` 打开媒体文件并用 `avformat_find_stream_info` 读取流信息。在主循环中，反复调用 `av_read_frame` 从文件中读取数据包 (`AVPacket`)，并根据其流索引，分别推入音频或视频的 `PacketQueue` 中。

**2.  视频解码线程 (Video Decode Thread)**: 

- 循环地从 `PacketQueue` 中取出 `AVPacket`，通过 `avcodec_send_packet` 将其发送给解码器。然后，通过 `avcodec_receive_frame` 接收解码完成的 `AVFrame`，并将其存入 `FrameQueue` 以供渲染线程使用。

**3.  视频渲染线程 (Video Render Thread)**: 

- 循环地从 `FrameQueue` 中取出 `AVFrame`。根据该帧的 PTS 和同步时钟的当前时间，计算出需要延迟的时间并等待。之后，使用 `sws_scale` (如果需要) 进行图像格式转换，再调用 `SDL_UpdateYUVTexture`, `SDL_RenderCopy`, `SDL_RenderPresent` 等函数将图像更新并显示在窗口上。

**4. 音频解码线程 (Audio Decode Thread)**:

- 与视频解码线程类似。循环地从音频 `PacketQueue` 中取出 `AVPacket`，通过 `avcodec_send_packet` 和 `avcodec_receive_frame` 解码出 `AVFrame`，并将其存入音频 `FrameQueue`。

**5. 音频渲染线程 (Audio Render Thread)**:

- 此线程以主动推送（Push）模式工作。它在循环中不断从音频帧队列 (`FrameQueue`) 中取出 `AVFrame`。如果音频参数与设备不符，会调用 `swr_convert` 进行重采样，得到最终的 PCM 数据。随后，线程会检查 SDL 音频队列的缓冲情况（通过 `SDL_GetQueuedAudioSize`），在队列未满时，主动调用 `SDL_QueueAudio` 将 PCM 数据推送至 SDL 的内部缓冲区，交由其独立播放。这种方式将解码/重采样逻辑与 SDL 的高优先级播放线程解耦，并通过控制推送节奏实现了自然的流量控制。**同时，此模块会根据推入队列的音频数据量，持续、精确地更新主时钟**。


## 2. 核心机制实现

### 2.1 缓存队列和流量控制逻辑

在多线程音视频播放器中，各个处理阶段（如解复用、解码、渲染）运行在独立的线程上。为了高效、稳定地在这些线程间传递数据，并协调它们的生产和消费速率，一个健壮的线程安全缓存队列是整个并发架构的基础。

在本项目中，设计并实现了 `PacketQueue` 和 `FrameQueue`（二者设计思想和结构类似），它们是基于生产者-消费者模式的高度并发组件。其核心职责是：

- **线程解耦**：允许解复用线程、解码线程和渲染线程独立运行，避免因某个阶段的瞬时阻塞（如I/O等待、解码耗时）而导致整个播放器停滞。

- **流量控制**：通过设置队列的最大容量 (`max_size`)，实现“背压”机制（Back-pressure）。当队列满时，生产者（如解复用线程）会自动阻塞，等待消费者取走数据，从而防止内存无限增长。反之，当队列为空时，消费者会阻塞，等待生产者放入数据。

- **生命周期管理**：通过一个原子性的 `eof_signaled` 标志，实现对数据流结束（EOF, End-of-File）的精确控制，确保所有线程都能在数据处理完毕后正常、有序地退出。

#### 2.1.1 核心设计

`PacketQueue` 的并发安全和流量控制主要依赖于以下C++标准库组件和设计模式：

- **std::mutex**：互斥锁，用于保护共享资源（即内部的 `std::queue`），确保任何时候只有一个线程能修改队列。

- **双条件变量** (`std::condition_variable`)：

  - `cond_producer`: 当队列已满时，用于阻塞生产者线程。

  - `cond_consumer`: 当队列为空时，用于阻塞消费者线程。
  
    使用两个独立的条件变量可以避免不必要的线程唤醒（例如，`push` 操作只应该唤醒等待数据的消费者，而不是等待空间的生产者）。

- **循环检查 (`while`循环)**：在调用 `wait()` 时，使用 `while` 循环来重新检查条件 (`while (queue.size() >= max_size)`)。这是为了防止“虚假唤醒”（Spurious Wakeups），确保线程被唤醒后，其等待的条件确实已满足。

- **EOF信令**：`signal_eof()` 方法通过设置 `eof_signaled` 标志并调用 `notify_all()`，可以唤醒所有正在等待的生产者和消费者线程。这些线程被唤醒后，会检查到 `eof_signaled` 为 `true`，从而能够优雅地退出其工作循环，结束线程。

#### 2.1.2 线程交互机制

下面通过UML序列图来展示两个核心场景下的线程交互逻辑。

**场景一：生产者在队列已满时尝试推入数据**

此场景展示解复用线程（生产者）如何因为视频 `Packet` 队列已满而阻塞，直到解码线程（消费者）消费了一个 `Packet` 后，生产者才被唤醒并继续执行。

![缓存队列对生产者的阻塞示意图](assets/pic4-queue_producer_block.svg)

这是一个多线程“生产者-消费者”模型的典型场景，具体参与者和同步机制如下：

- **生产者 (Producer)**: `DemuxThread` (解复用线程)
- **消费者 (Consumer)**: `VideoDecodeThread` (视频解码线程)
- **共享资源 (Shared Resource)**: `PacketQueue` (数据包队列)
- **锁管理者 (Lock Manager)**: `std::unique_lock`，它以RAII方式管理底层互斥锁。
- **底层同步原语 (Synchronization Primitives)**: `std::mutex` (由`unique_lock`管理) 和 `cond_producer` (条件变量)。

线程交互逻辑的逐步分解如下：

**1. 生产者尝试推送数据**:

- 生产者 `DemuxThread` 调用 `PacketQueue` 的 `push` 方法，希望放入一个数据包。`PacketQueue` 的生命线被激活，开始执行 `push` 方法。

**2. 通过RAII获取锁**:

- 进入 `push` 函数后，代码首先创建一个 `std::unique_lock` 实例 (`Lock`)。这体现了C++的 **RAII (资源获取即初始化)** 原则：`lock` 对象在**构造时**会自动锁定其管理的 `mutex`。这确保了在 `push` 函数的后续作用域内，队列访问是线程安全的。

**3. 检查等待条件**:

- 在持有锁的情况下，线程进入 `while` 循环检查条件 (`queue.size() >= max_size`)。由于此时队列已满，条件为真，生产者不能立即推入数据，必须等待。

**4. 生产者等待并原子性地释放锁**:

- 生产者调用 `cond_producer.wait(lock)`，将 `lock` 管理器作为参数传入。这是一个关键**原子操作**，它能保证：
  - **自动释放** `lock` 所管理的 `mutex`。
  - **阻塞**当前生产者线程，使其进入等待状态。
- 图中 `Lock` 的生命线暂时失活 (`deactivate`)，直观地表示了锁已被释放，从而允许其他线程（如消费者）获取它。

**5. 并发的消费者操作**:

- 在生产者线程等待期间，另一个 `VideoDecodeThread` (消费者) 线程可以自由执行。它会调用 `pop()` 方法，成功获取同一个互斥锁（因为生产者已在 `wait` 中释放了它），从队列中取出一个元素，然后调用 `cond_producer.notify_one()`。此调用会发送一个信号，旨在唤醒一个正在等待“队列已满”条件的生产者线程。

**6. 生产者被唤醒并重新获取锁**:

- 接收到消费者的通知后，生产者线程被唤醒。`wait(lock)` 函数在**返回之前**，会**自动重新获取**它之前释放的锁。`lock` 的生命线被重新激活。
- `wait` 函数返回后，代码从 `while` 循环的头部继续执行，**再次检查条件**。这是为了防止“虚假唤醒”。此时队列已有空间，条件为假，循环退出。

**7. 生产者完成数据推送**:

- 循环退出后，生产者成功将新的数据包 (`pkt_clone`) 推入队列。

**8. 提前解锁以提高并发**:

- 在通知消费者之前，代码显式调用 `lock.unlock()` **提前手动释放锁**。这是一个性能优化，它减小了锁的持有时间（即减小锁的粒度），允许其他线程（如另一个消费者）能够更快地访问队列，从而提高整体并发性能。`lock` 的生命线再次失活。

**9. 通知消费者并返回**:

- 在释放锁之后，生产者调用 `cond_consumer.notify_one()` 来通知任何可能因“队列为空”而等待的消费者线程。
- 最后，`push` 方法执行完毕，向生产者 `DemuxThread` 成功返回。

**场景二：消费者在队列为空时尝试拉取数据**

此场景展示了解码线程（消费者）如何因为 `Packet` 队列为空而阻塞，并带有超时机制。

![缓存队列对消费者的阻塞示意图](assets/...)

具体参与者是：

- **消费者 (Consumer)**: `VideoDecodeThread` (视频解码线程)
....


线程交互逻辑的逐步分解如下：

**1. 消费者尝试拉取数据**:

- ...
- ...

**2. ...**:


### 2.2 音视频同步逻辑

本播放器采用“音频驱动视频 (Audio-Master)”的同步策略...

![音视频同步线程交互图](assets/av_sync_interaction.png)


### 2.3 播放/暂停机制
