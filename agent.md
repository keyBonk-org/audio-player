# Yumo Audio 音频混合播放库

## 项目概述

这是一个轻量级的 Windows 音频混合播放库，用于解决 Win32 `waveOut` 系列 API 的冲突问题。

### 核心定位

- **不同时播放**：解决原生 API 同一时间只能播放一个音频的问题
- **灵活控制**：支持预加载、动态添加播放、静音控制等
- **混合播放**：多个音频可以同时播放并混合

### 设计目标

- `addAudio` 添加音频后立即播放
- 提供 `preloadAudio` 接口允许预先加载标准音频对象
- 支持 `stopAll` 静音功能
- 允许同一音频的重复播放（通过播放实例追踪独立位置）

## 依赖库

| 库 | 说明 |
|----|------|
| Windows ACM | 系统自带音频转换库（msacm32.lib） |
| winmm | Windows 多媒体 API |

### 编译命令

```powershell
g++ -std=c++17 -I"src" -o output.exe source.cpp src/audioPlayer.cpp -lmsacm32 -lwinmm -lcomdlg32
```

### 编译中间产物

编译过程中生成的中间文件应放置在以下被 `.gitignore` 忽略的路径：

| 文件类型 | 存放路径 | 说明 |
|----------|----------|------|
| `.exe` 测试可执行文件 | `test/compiled/` | 测试程序输出 |

**不要将中间产物放在项目根目录**，以免污染源码仓库。

### 测试文件编译

测试文件编译至：`./test/compiled/`

## API 接口

### 核心接口

| 接口 | 说明 |
|------|------|
| `AudioPool::getInstance()` | 获取单例实例 |
| `preloadAudio(filename, ready)` | 异步预加载音频文件 |
| `addAudio(preloadedId)` | 添加预加载音频并立即播放 |
| `addAudio(filename)` | 简化用法：异步加载并播放 |
| `stopAll()` | 停止所有播放（挂起） |
| `resume()` | 恢复播放 |

### 查询接口

| 接口 | 说明 |
|------|------|
| `getPreloadedCount()` | 获取预加载音频数量 |
| `getPlayingCount()` | 获取当前播放实例数量 |
| `isPlaying(instanceId)` | 检查播放实例是否正在播放 |

### 控制接口

| 接口 | 说明 |
|------|------|
| `setVolume(instanceId, volume)` | 设置播放实例音量 |
| `setMuted(muted)` | 设置全局静音状态 |
| `resetAll()` | 重置所有播放实例位置到开头 |

## 数据结构

### PreloadedAudio

存储共享的音频数据，同一个预加载音频可创建多个播放实例。

```cpp
struct PreloadedAudio {
    StandardWavInfo data;  // 重采样后的音频数据（44.1kHz, 双声道, 16位）
};
```

### PlayInstance

每次 `addAudio` 创建，追踪独立的播放位置。

```cpp
struct PlayInstance {
    PreloadedAudio* source;  // 指向共享的预加载音频数据
    size_t position;         // 当前播放位置（样本索引）
    float volume;            // 音量（0.0-1.0）
    bool active;             // 是否激活播放
};
```

### StandardWavInfo

标准音频格式：`std::vector<int16_t>` - 44.1kHz, 双声道, 16位 PCM

## 核心实现

### 音频格式

| 参数 | 值 |
|------|-----|
| 采样率 | 44.1kHz |
| 声道数 | 双声道 |
| 位深度 | 16位 |
| 缓冲区大小 | 8820 样本（100ms） |

### 双缓冲技术

使用两个音频缓冲区交替播放，避免音频断续：

1. `playAll()` 或首次 `addAudio` 时预先准备2个缓冲区
2. `waveOutCallback` 在缓冲区播放完成后准备下一个缓冲区
3. 保证音频设备始终有数据可播放

### 异步加载

`preloadAudio` 使用后台线程异步加载，不阻塞音频播放：

1. 主线程预留位置并返回 ID
2. 后台线程加载 WAV 文件并使用 Windows ACM 重采样
3. 通过 `std::atomic<bool>` 标记加载完成状态
4. `addAudio(preloadedId)` 检查数据就绪后创建播放实例

### Windows ACM 重采样

使用 Windows ACM API 进行音频格式转换：

1. `acmStreamOpen` - 打开转换流
2. `acmStreamSize` - 计算目标缓冲区大小
3. `acmStreamPrepareHeader` - 准备转换头
4. `acmStreamConvert` - 执行转换
5. `acmStreamClose` - 关闭转换流

### 线程安全

- 使用 `std::mutex` 保护共享数据
- `std::atomic<bool>` 用于跨线程状态同步
- 使用 `std::unique_ptr` 管理动态分配的预加载音频数据，避免 vector 扩容导致的悬空指针
- 所有公开接口都是线程安全的

## 使用示例

### 简化用法（快速播放）

```cpp
AudioPool& pool = AudioPool::getInstance();
pool.addAudio(L"test.wav");  // 异步加载，立即返回
pool.addAudio(L"test2.wav");  // 混合播放
```

### 标准用法（预加载 + 控制）

```cpp
AudioPool& pool = AudioPool::getInstance();
std::atomic<bool> ready(false);

// 预加载
size_t id1 = pool.preloadAudio(L"test.wav", ready);
while (!ready.load()) Sleep(10);

// 添加播放
size_t instanceId = pool.addAudio(id1);
pool.setVolume(instanceId, 0.5f);

// 停止
pool.stopAll();

// 恢复
pool.resume();
```

## 注意事项

1. **路径问题**：使用相对路径时注意工作目录，测试程序从 `test/compiled/` 运行
2. **音频播放完毕**：播放实例不会自动清理，需手动 `stopAll()`
3. **设备状态**：`stopAll()` 不会关闭音频设备，只是停止并重置位置
4. **内存管理**：音频数据由库管理，调用者无需释放
5. **停止与恢复**：`stopAll()` 后需要调用 `resume()` 才能重新播放

## 测试文件

| 文件 | 说明 |
|------|------|
| `test/1_readAudioInfo.cpp` | WAV 文件信息读取测试 |
| `test/2_resample_test.cpp` | 音频重采样测试 |
| `test/3_audio_pool_test.cpp` | addAudio 字符串版本测试 |
| `test/4_audio_mix_test.cpp` | 预加载 + 延迟添加测试 |
| `test/5_simple_test.cpp` | 单音频播放测试 |
| `test/6_play_control_test.cpp` | 播放控制测试 |

## 文件结构

```
audio-player/
├── src/
│   ├── audioPlayer.hpp    # 头文件
│   └── audioPlayer.cpp    # 实现
├── test/
│   ├── audio/             # 测试音频文件
│   ├── compiled/          # 编译输出目录
│   ├── build_manager.py   # 编译管理脚本
│   └── *.cpp              # 测试源文件
├── README.md              # 项目说明
└── agent.md               # 本文档
```

## AI开发须知

每次修改完库源码，编译对应的测试文件到 compile 路径下，以便用户检测，本要求在库提供一个建议的测试文件编译方案前永久生效

### 编译管理脚本

使用 `build_manager.py` 脚本编译测试文件：

```powershell
cd test
python build_manager.py
```

脚本支持：
- 按序号选择编译文件（如 `1 3 4-6`）
- `all` 选项编译全部测试文件
- 自动输出到 `test/compiled/` 目录