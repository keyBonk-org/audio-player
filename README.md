# Yumo Audio

Yumo Audio是利用WIN32 API实现的C++音频播放库，主要聚焦于实现音频混合等基础API无法满足的功能，属于自用型库，完全服务于[keyBonk](https://github.com/xiaoditx/keyBonk)所以有一些通用功能将不予考虑

由于上文提到的自用特性，本库结构异常混乱，不支持CMake构建，部分文件缺失需要手动补齐，因此暂不建议直接使用。如果必须使用，请自行改造本库。

本库初期版本基本为纯vibe产物，后期由人工进行，目前所有vibe代码已经经过了人工初审，但仍请谨慎使用。

目前开发进度：基本实现，接口已经基本实现，但是格式支持于兼容性还有待提升

## 一. 获取库文件

可以从[release](https://github.com/keyBonk-org/audio-player/releases)中获取库文件，也可以通过手动构建来实现

手动构建流程如下：

```powershell
git clone https://github.io/keybonk-org/audio-player/
cd audio-player
make          # 获取本地 g++ 编译的版本
make 32       # 获取 32 位版本（需 i686-w64-mingw32-g++）
```

参考文档“[为make管理的项目引入Yumo audio](https://keybonk-org.github.io/docs/audio-player/make_get/)”可以为通过make管理的项目引入Yumo audio并实现自动下载

## 二. 示例

播放音频：

```cpp
#include <yumo/audio.hpp>
#include <chrono>
#include <thread>

int main() {
    yumo::readySign ready(false);
    auto preloadId = yumo::preloadAudio(L"bgm.wav", &ready);

    while (!ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto inst = yumo::addAudio(preloadId, 0.8f);
}
```

控制播放状态：

```cpp
auto inst = yumo::addAudio(preloadId, 1.0f);

inst.volume = 0.5f;          // 音量 50%
inst.stopped = true;         // 暂停
inst.stopped = false;        // 继续播放
inst.muted = true;           // 静音（位置继续推进）
inst.muted = false;          // 取消静音
inst.position = 44100 * 10;  // 跳到第 10 秒（按采样点计）
```

## 三. 兼容性

### 1.平台
Windows独占

### 2.文件格式

（格式按扩展名记）

| 格式 | 是否支持 | 支持程度 |
|:------:|:---------:|:----------:|
|WAV   |支持     |不支持非PCM格式以及超过2声道的WAV文件|
|PCM   |即将支持  | -       |
|MP3   |支持     | 支持大多MP3文件 |
|MP4   |不支持    | -       |
|M4A   |不支持    | -       |
|OGG   |不支持    | -       |
|FLAC  |不支持    | -       |
|WMA   |不支持    | -       |

### 3.是否兼容C语言

Yumo Audio大量采用C++特性编写，不兼容C语言，虽然可以通过函数包装链接到C，但库内不提供这些包装

## 四. 技术栈

- C++
- WIN32 API

## 五. 文档

- [开始使用 Yumo audio](https://keybonk-org.github.io/docs/audio-player/usage/)
- [音频库选择指南](https://keybonk-org.github.io/docs/audio-player/choice/)
