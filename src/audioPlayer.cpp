#ifndef UNICODE
#define UNICODE
#endif

#include "audioPlayer.hpp"
#include "yumo_except.hpp"
#include <iostream>
#include <cassert>
#include <functional>
#include <thread>
#include <queue>
#include <windows.h>
#include <mmreg.h>
#include <msacm.h>
#include <stdio.h>
#include <memory>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <map>
#include <mutex>

namespace
{
    // 资源管理类，自动释放资源
    template <typename resourceType>
    class resourceManager
    {
    private:
        resourceType resource;         // 资源句柄或指针
        resourceType invalidValue;     // 资源无效值
        void (*deleter)(resourceType); // 资源释放函数指针

    public:
        resourceManager(
            resourceType res, resourceType invalid, void (*del)(resourceType))
            : resource(res), invalidValue(invalid), deleter(del) {}
        ~resourceManager()
        {
            if (resource != invalidValue)
                deleter(resource);
        }
        // 禁止拷贝，允许移动
        resourceManager(const resourceManager &) = delete;
        resourceManager &operator=(const resourceManager &) = delete;
        resourceType get() { return resource; }
    };

    // todo 支持WAVE_FORMAT_IEEE_FLOAT、WAVE_FORMAT_EXTENSIBLE等非PCM格式
    void ValidatePcmFormat(const WAVEFORMATEX &wf)
    {
        if (wf.wFormatTag != WAVE_FORMAT_PCM)
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"仅支持 PCM 格式");
        if (wf.nChannels != 1 && wf.nChannels != 2)
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"仅支持单声道或立体声");
        if (wf.wBitsPerSample != 8 && wf.wBitsPerSample != 16 && wf.wBitsPerSample != 24 && wf.wBitsPerSample != 32)
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"仅支持 8/16/24/32 位深度");
        if (wf.nSamplesPerSec < 8000 || wf.nSamplesPerSec > 192000)
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"采样率超出合理范围");
    }

    // 本文件内部常量表
    const DWORD RIFF_ID = 0x46464952;                    // "RIFF" 的小端序整数值
    const DWORD WAVE_ID = 0x45564157;                    // "WAVE" 的小端序整数值
    const DWORD FMT_ID = 0x20746D66;                     // "fmt " 的小端序整数值（注意空格）
    const DWORD DATA_ID = 0x61746164;                    // "data" 的小端序整数值
    const size_t MINIMUM_FMT_SIZE = 16;                  // 最小fmt块大小（包含所有必要字段）
    const size_t MAXIMUM_FMT_SIZE = 24;                  // 最大fmt块大小（包含所有可选字段）
    const size_t COMMON_FMT_SIZE = sizeof(WAVEFORMATEX); // fmt块大小（包含所有字段）

    /**
     * @brief WAV文件信息结构体
     *
     * 用于存储WAV文件的格式信息和音频数据
     *
     * - wf: WAVEFORMATEX结构体，包含采样率、声道数、位深度等音频格式信息
     *
     * - pcmData: 指向PCM原始数据缓冲区的指针
     *
     * - dataSize: PCM数据的大小（字节数）
     *
     * - valid: 标志位，指示是否成功加载并解析WAV文件
     */
    struct WavInfo
    {
        WAVEFORMATEX wf;                 // 音频格式信息（采样率、声道数、位深度等）
        std::unique_ptr<char[]> pcmData; // 指向PCM原始数据缓冲区的指针
        DWORD dataSize;                  // PCM数据的大小（字节数）
        bool valid = false;              // 是否成功加载并解析WAV文件
    };

    typedef std::vector<int16_t> StandardWavInfo;

    /**
     * @brief 预加载音频信息结构体
     *
     * 存储重采样后的标准格式音频数据，作为共享数据源
     * 同一个预加载音频可以被多次添加播放，每次创建独立的播放实例
     */
    struct PreloadedAudio
    {
        StandardWavInfo data;          // 重采样后的音频数据（44.1kHz, 双声道, 16位）
        bool markedForRemoval = false; // 标记为待删除（播放结束后清理）
    };

    /**
     * @brief 播放实例结构体
     *
     * 每次 addAudio 创建一个播放实例，追踪独立的播放位置
     * 多个实例可以引用同一个预加载音频，实现同一音频的重复播放
     */
    struct PlayInstance
    {
        PreloadedAudio *source; // 指向共享的预加载音频数据
        size_t position;        // 当前播放位置（样本索引）
        float volume;           // 音量（0.0-1.0）
        bool active;            // 是否激活播放
        bool stopped;           // 是否暂停（与active区别：stopped时位置不推进）
        bool muted;             // 是否静音（跳过混音但位置继续推进）
    };

    /**
     * @brief 音频池类 - 单例模式
     *
     * 管理多个音频文件的预加载和混合播放
     * 支持同一音频的重复播放，每个播放实例独立追踪位置
     */
    class AudioPool
    {
    public:
        /**
         * @brief 获取单例实例
         */
        static AudioPool &getInstance();

        /**
         * @brief 预加载音频文件（异步）
         *
         * 将WAV文件加载并重采样为标准格式，供后续添加播放
         * 加载在后台线程进行，不阻塞播放
         *
         * @param filename WAV文件路径
         * @param ready 可选的加载状态标记（调用时设为false，加载完成后变为true）
         * @return 预加载音频ID（用于后续 addAudio）
         */
        size_t preloadAudio(const wchar_t *filename, yumo::readySign *ready = nullptr);

        /**
         * @brief 添加预加载音频到播放池并立即播放
         *
         * 创建一个新的播放实例，与当前播放的音频混合
         *
         * @param preloadedId preloadAudio 返回的预加载音频ID
         * @param volume 音量（0.0-1.0），默认为 1.0
         * @return 播放实例ID
         */
        size_t addAudio(size_t preloadedId, float volume = 1.0f);

        /**
         * @brief 添加音频文件到播放池并立即播放（便利接口）
         *
         * 简化用法，内部自动完成异步预加载和添加播放。
         * 播放完成后自动移除预加载对象（用完即弃）。
         *
         * @param filename WAV文件路径
         * @param volume 音量（0.0-1.0），默认为 1.0
         * @param instanceId 可选的播放实例ID输出（播放开始后写入）
         * @param ready 可选的加载状态标记（加载完成后变为true）
         */
        void addAudio(const wchar_t *filename, float volume = 1.0f, size_t *instanceId = nullptr, yumo::readySign *ready = nullptr);

        /**
         * @brief 移除预加载音频对象
         *
         * 从预加载队列中移除不再需要的音频对象。
         * 注意：如果该预加载对象正在被播放实例引用，播放会继续直到结束。
         *
         * @param preloadedId preloadAudio 返回的预加载音频ID
         */
        void removePreloadedAudio(size_t preloadedId);

        /**
         * @brief 获取预加载音频数量
         */
        size_t getPreloadedCount() const;

        /**
         * @brief 获取当前播放实例数量
         */
        size_t getPlayingCount() const;

        /**
         * @brief 检查指定ID的播放实例是否正在播放
         */
        bool isPlaying(size_t instanceId) const;

        /**
         * @brief 重置所有播放实例的位置到开头
         */
        void resetAll();

        /**
         * @brief 设置播放实例音量
         *
         * @param instanceId 播放实例ID
         * @param volume 音量（0.0-1.0）
         */
        void setVolume(size_t instanceId, float volume);

        /**
         * @brief 获取播放实例音量
         */
        float getVolume(size_t instanceId) const;

        /**
         * @brief 停止指定播放实例
         *
         * @param instanceId 播放实例ID
         * @return true=成功，false=无效ID
         */
        bool stop(size_t instanceId);

        /**
         * @brief 恢复指定播放实例
         *
         * @param instanceId 播放实例ID
         * @return true=成功，false=无效ID
         */
        bool resume(size_t instanceId);

        /**
         * @brief 设置指定播放实例的静音状态
         *
         * @param instanceId 播放实例ID
         * @param muted true=静音，false=取消静音
         * @return true=成功，false=无效ID
         */
        bool setMuted(size_t instanceId, bool muted);

        /**
         * @brief 移除播放实例
         *
         * 从播放队列中移除指定实例，释放其资源
         *
         * @param instanceId 播放实例ID
         * @return true=成功，false=无效ID
         */
        bool remove(size_t instanceId);

        // 禁用拷贝构造和赋值
        AudioPool(const AudioPool &) = delete;
        AudioPool &operator=(const AudioPool &) = delete;

    private:
        AudioPool() = default;
        ~AudioPool() = default;

        size_t allocateInstanceId();

        std::vector<std::unique_ptr<PreloadedAudio>> preloadedAudios_;
        std::map<size_t, PlayInstance> playInstances_;
        std::queue<size_t> freeInstanceIds_;
        size_t nextInstanceId_ = 0;
        mutable std::mutex mutex_;
        bool isPlaying_ = false;
        HWAVEOUT hWaveOut_ = nullptr;

        static const size_t BUFFER_COUNT = 2;

        static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

        void mixAudioChunk(int16_t *output, size_t chunkSize);

        void ensureDeviceOpen();
    };

    // 前向声明（内部使用的辅助函数）
    StandardWavInfo convertToStandard(const WavInfo &wavInfo);
    void loadWav(const wchar_t *filename, WavInfo *out);

    // 音频缓冲区大小（约100ms的音频）
    // 计算：采样率 * 声道数 * 时长(秒) = 44100 * 2 * 0.1 = 8820 个样本
    const size_t AUDIO_CHUNK_SIZE = static_cast<size_t>(44100 * 2 * 0.1);

    // AudioPool 单例实现
    AudioPool &AudioPool::getInstance()
    {
        static AudioPool instance;
        return instance;
    }

    // 预加载音频文件（异步）
    size_t AudioPool::preloadAudio(const wchar_t *filename, yumo::readySign *ready)
    {
        auto readyPtr = ready
                            ? std::shared_ptr<yumo::readySign>(ready, [](yumo::readySign *) {})
                            : std::make_shared<yumo::readySign>(false);

        if (ready)
        {
            *ready = false;
        }

        size_t preloadedId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            preloadedAudios_.push_back(std::make_unique<PreloadedAudio>());
            preloadedId = preloadedAudios_.size() - 1;
        }

        std::thread loadThread([this, filename, preloadedId, readyPtr]()
                               {
            try {
                WavInfo wavInfo;
                loadWav(filename, &wavInfo);

                StandardWavInfo standardData = convertToStandard(wavInfo);

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    preloadedAudios_[preloadedId]->data = std::move(standardData);
                }

                *readyPtr = true;
            } catch (...) {
                *readyPtr = true;
            } });

        loadThread.detach();

        return preloadedId;
    }

    // 分配新的播放实例ID（使用回收机制）
    size_t AudioPool::allocateInstanceId()
    {
        if (!freeInstanceIds_.empty())
        {
            size_t id = freeInstanceIds_.front();
            freeInstanceIds_.pop();
            return id;
        }
        return nextInstanceId_++;
    }

    // 添加预加载音频并立即播放
    size_t AudioPool::addAudio(size_t preloadedId, float volume)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (preloadedId >= preloadedAudios_.size())
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"无效的预加载音频ID");

        // 检查数据是否已加载
        if (preloadedAudios_[preloadedId]->data.empty())
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"预加载音频数据为空");

        // 创建播放实例
        PlayInstance instance;
        instance.source = preloadedAudios_[preloadedId].get();
        instance.position = 0;
        instance.volume = volume;
        instance.active = true;
        instance.stopped = false;
        instance.muted = false;

        size_t instanceId = allocateInstanceId();
        playInstances_[instanceId] = instance;

        // 如果设备未打开，则打开
        if (!hWaveOut_)
        {
            lock.unlock();
            ensureDeviceOpen();
        }

        return instanceId;
    }

    // 添加音频文件并立即播放（便利接口）
    void AudioPool::addAudio(const wchar_t *filename, float volume, size_t *instanceId, yumo::readySign *ready)
    {
        auto readyPtr = ready
                            ? std::shared_ptr<yumo::readySign>(ready, [](yumo::readySign *) {})
                            : std::make_shared<yumo::readySign>(false);

        auto instanceIdPtr = instanceId
                                 ? std::shared_ptr<size_t>(instanceId, [](size_t *) {})
                                 : std::make_shared<size_t>(0);

        if (ready)
        {
            *ready = false;
        }

        size_t preloadedId = preloadAudio(filename, readyPtr.get());

        std::thread([this, preloadedId, readyPtr, instanceIdPtr, volume]()
                    {
            while (!readyPtr->load()) {
                Sleep(10);
            }

            readyPtr->store(false);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (preloadedId >= preloadedAudios_.size() || preloadedAudios_[preloadedId]->data.empty()) {
                    readyPtr->store(true);
                    return;
                }
            }

            try {
                size_t id = addAudio(preloadedId, volume);
                *instanceIdPtr = id;

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (preloadedId < preloadedAudios_.size()) {
                        preloadedAudios_[preloadedId]->markedForRemoval = true;
                    }
                }

                readyPtr->store(true);
            } catch (...) {
                readyPtr->store(true);
            } })
            .detach();
    }

    // 移除预加载音频对象
    void AudioPool::removePreloadedAudio(size_t preloadedId)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (preloadedId >= preloadedAudios_.size())
        {
            return;
        }

        // 检查是否有播放实例正在引用该预加载对象
        bool isReferenced = false;
        for (const auto &inst : playInstances_)
        {
            if (inst.second.active && inst.second.source == preloadedAudios_[preloadedId].get())
            {
                isReferenced = true;
                break;
            }
        }

        if (!isReferenced)
        {
            // 没有被引用，可以安全删除
            preloadedAudios_[preloadedId].reset();
        }
        else
        {
            // 被引用，标记为待删除，播放结束后删除
            preloadedAudios_[preloadedId]->markedForRemoval = true;
        }
    }

    // 获取预加载音频数量
    size_t AudioPool::getPreloadedCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return preloadedAudios_.size();
    }

    // 获取当前播放实例数量
    size_t AudioPool::getPlayingCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return playInstances_.size();
    }

    // 检查播放实例是否正在播放
    bool AudioPool::isPlaying(size_t instanceId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            return false;
        const auto &inst = it->second;
        return inst.active && inst.source && inst.position < inst.source->data.size();
    }

    // 混合一小段音频
    void AudioPool::mixAudioChunk(int16_t *output, size_t chunkSize)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 如果停止（挂起），输出静默但不推进position
        if (yumo::global.stop)
        {
            memset(output, 0, chunkSize * sizeof(int16_t));
            return;
        }

        // 清空输出缓冲区
        memset(output, 0, chunkSize * sizeof(int16_t));

        // 混合所有激活的播放实例
        for (auto &inst : playInstances_)
        {
            if (!inst.second.active || !inst.second.source || inst.second.position >= inst.second.source->data.size())
                continue;

            // 如果该实例被暂停（stop），位置不推进（真正暂停）
            if (inst.second.stopped)
            {
                continue;
            }

            // 如果全局静音或实例静音，跳过混音但继续推进position
            if (yumo::global.mute || inst.second.muted)
            {
                size_t remaining = inst.second.source->data.size() - inst.second.position;
                size_t samplesToAdvance = std::min(chunkSize, remaining);
                inst.second.position += samplesToAdvance;
                continue;
            }

            const auto &data = inst.second.source->data;
            size_t remaining = data.size() - inst.second.position;
            size_t samplesToCopy = std::min(chunkSize, remaining);

            for (size_t i = 0; i < samplesToCopy; ++i)
            {
                // 应用全局音量和实例音量并混合
                int32_t mixed =
                    static_cast<int32_t>(output[i]) +
                    static_cast<int32_t>(
                        data[inst.second.position + i] *
                        inst.second.volume *
                        yumo::global.volume.load());

                // 防止溢出
                if (mixed > INT16_MAX)
                    mixed = INT16_MAX;
                if (mixed < INT16_MIN)
                    mixed = INT16_MIN;

                output[i] = static_cast<int16_t>(mixed);
            }

            // 更新播放位置
            inst.second.position += samplesToCopy;
        }
    }

    // 音频播放回调
    void CALLBACK AudioPool::waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, [[maybe_unused]] DWORD_PTR dwParam2)
    {
        if (uMsg != WOM_DONE)
            return;

        WAVEHDR *pHeader = reinterpret_cast<WAVEHDR *>(dwParam1);
        AudioPool *pPool = reinterpret_cast<AudioPool *>(dwInstance);

        if (!pPool || !pHeader)
            return;

        // 先释放已播放完的缓冲区
        waveOutUnprepareHeader(hwo, pHeader, sizeof(WAVEHDR));
        delete[] pHeader->lpData;
        delete pHeader;

        // 检查是否所有音频都播放完毕
        bool allFinished = true;
        {
            std::lock_guard<std::mutex> lock(pPool->mutex_);
            if (!pPool->isPlaying_)
            {
                return; // 已停止播放
            }
            // 如果有激活的实例还没播完，就继续
            for (const auto &inst : pPool->playInstances_)
            {
                if (inst.second.active && inst.second.source && inst.second.position < inst.second.source->data.size())
                {
                    allFinished = false;
                    break;
                }
            }

            // 清理标记为待删除且没有被引用的预加载对象
            for (size_t i = 0; i < pPool->preloadedAudios_.size(); ++i)
            {
                if (pPool->preloadedAudios_[i] && pPool->preloadedAudios_[i]->markedForRemoval)
                {
                    bool isReferenced = false;
                    for (const auto &inst : pPool->playInstances_)
                    {
                        if (inst.second.active && inst.second.source == pPool->preloadedAudios_[i].get())
                        {
                            isReferenced = true;
                            break;
                        }
                    }
                    if (!isReferenced)
                    {
                        pPool->preloadedAudios_[i].reset();
                    }
                }
            }
        }

        if (allFinished)
        {
            // 所有音频播放完毕，关闭设备
            std::lock_guard<std::mutex> lock(pPool->mutex_);
            pPool->isPlaying_ = false;
            waveOutClose(hwo);
            pPool->hWaveOut_ = nullptr;
            return;
        }

        // 准备新的数据
        int16_t *pData = new int16_t[AUDIO_CHUNK_SIZE];
        pPool->mixAudioChunk(pData, AUDIO_CHUNK_SIZE);

        WAVEHDR *pNewHeader = new WAVEHDR;
        memset(pNewHeader, 0, sizeof(WAVEHDR));
        pNewHeader->lpData = reinterpret_cast<LPSTR>(pData);
        pNewHeader->dwBufferLength = static_cast<DWORD>(AUDIO_CHUNK_SIZE * sizeof(int16_t));

        waveOutPrepareHeader(hwo, pNewHeader, sizeof(WAVEHDR));
        waveOutWrite(hwo, pNewHeader, sizeof(WAVEHDR));
    }

    // 确保音频设备已打开
    void AudioPool::ensureDeviceOpen()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (hWaveOut_)
            return;

        if (playInstances_.empty())
            return;

        // 只重置还在播放的实例的位置（position < data.size()）
        // 已播放完毕的实例不重置，让它们保持结束状态
        for (auto &inst : playInstances_)
        {
            if (inst.second.active && inst.second.source && inst.second.position < inst.second.source->data.size())
            {
                inst.second.position = 0;
            }
        }

        // 设置播放格式：44.1kHz, 双声道, 16位
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 2;
        wf.nSamplesPerSec = 44100;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = wf.nChannels * wf.wBitsPerSample / 8;
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

        HWAVEOUT hWaveOut = NULL;

        lock.unlock();

        MMRESULT mmResult = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wf,
                                        reinterpret_cast<DWORD_PTR>(waveOutCallback),
                                        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);

        if (mmResult != MMSYSERR_NOERROR)
        {
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"波形音频设备打开失败");
        }

        lock.lock();
        if (hWaveOut_)
        {
            waveOutClose(hWaveOut);
            lock.unlock();
            return;
        }
        hWaveOut_ = hWaveOut;
        isPlaying_ = true;
        lock.unlock();

        // 预先准备多个缓冲区（双缓冲）
        for (size_t i = 0; i < BUFFER_COUNT; ++i)
        {
            int16_t *pData = new int16_t[AUDIO_CHUNK_SIZE];
            mixAudioChunk(pData, AUDIO_CHUNK_SIZE);

            WAVEHDR *pHeader = new WAVEHDR;
            memset(pHeader, 0, sizeof(WAVEHDR));
            pHeader->lpData = reinterpret_cast<LPSTR>(pData);
            pHeader->dwBufferLength = static_cast<DWORD>(AUDIO_CHUNK_SIZE * sizeof(int16_t));

            waveOutPrepareHeader(hWaveOut, pHeader, sizeof(WAVEHDR));
            waveOutWrite(hWaveOut, pHeader, sizeof(WAVEHDR));
        }
    }

    // 重置所有播放实例的位置到开头
    void AudioPool::resetAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &inst : playInstances_)
        {
            inst.second.position = 0;
        }
    }

    // 设置播放实例音量
    void AudioPool::setVolume(size_t instanceId, float volume)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"无效的播放实例ID");

        it->second.volume = std::max(0.0f, std::min(1.0f, volume));
    }

    // 获取播放实例音量
    float AudioPool::getVolume(size_t instanceId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"无效的播放实例ID");

        return it->second.volume;
    }

    // 停止指定播放实例
    bool AudioPool::stop(size_t instanceId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            return false;

        it->second.stopped = true;
        return true;
    }

    // 恢复指定播放实例
    bool AudioPool::resume(size_t instanceId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            return false;

        it->second.stopped = false;
        return true;
    }

    // 设置指定播放实例的静音状态
    bool AudioPool::setMuted(size_t instanceId, bool muted)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            return false;

        it->second.muted = muted;
        return true;
    }

    // 移除播放实例
    bool AudioPool::remove(size_t instanceId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = playInstances_.find(instanceId);
        if (it == playInstances_.end())
            return false;

        playInstances_.erase(it);
        freeInstanceIds_.push(instanceId);
        return true;
    }

    // 从磁盘加载WAV文件，解析其格式块和数据块，填充WavInfo结构体
    void loadWav(const wchar_t *filename, WavInfo *out)
    {
        resourceManager<HANDLE> fileHandler(
            CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL),
            INVALID_HANDLE_VALUE,
            [](HANDLE h) -> void
            { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); });

        // 文件打开失败
        if (fileHandler.get() == INVALID_HANDLE_VALUE)
            throw yumo::exception_ex(
                yumo::exception::type::FileOpenError,
                L"WAV文件打开失败，可能是文件不存在或其他错误");

        DWORD riff, fileLenMinus8, wave;
        DWORD bytesRead;
        // 读取RIFF头：RIFF标识、文件长度（减去8字节）、WAVE标识
        // 这个if可能有点长，嘻嘻
        if (!ReadFile(fileHandler.get(), &riff, 4, &bytesRead, NULL) || bytesRead != 4 ||
            !ReadFile(fileHandler.get(), &fileLenMinus8, 4, &bytesRead, NULL) || bytesRead != 4 ||
            !ReadFile(fileHandler.get(), &wave, 4, &bytesRead, NULL) || bytesRead != 4)
        {
            throw yumo::exception_ex(
                yumo::exception::type::FileReadError,
                L"WAV文件读取失败，可能是文件损坏或其他错误");
        }

        // 检查是否为合法的WAV文件（RIFF和WAVE标记）
        if (riff != RIFF_ID || wave != WAVE_ID)
        {
            throw yumo::exception_ex(
                yumo::exception::type::InvalidInput,
                L"WAV文件的RIFF或WAVE标记存在问题，可能不是合法的WAV文件");
        }

        DWORD chunkId, chunkSize;
        // 循环读取各个块，直到找到fmt和data块
        while (true)
        {
            if (!ReadFile(fileHandler.get(), &chunkId, 4, &bytesRead, NULL) || bytesRead != 4 ||
                !ReadFile(fileHandler.get(), &chunkSize, 4, &bytesRead, NULL) || bytesRead != 4)
            {
                throw yumo::exception_ex(
                    yumo::exception::type::FileReadError,
                    L"无法从WAV文件读取到fmt或data块，可能是文件损坏或格式异常");
            }
            if (chunkId == FMT_ID) // "fmt " （注意空格，别给我删咯） 音频格式块
            {
                if (chunkSize < MINIMUM_FMT_SIZE)
                {
                    throw yumo::exception_ex(
                        yumo::exception::type::InvalidInput,
                        L"WAV文件的格式块大小异常，无法解析音频格式信息");
                }
                // 先读取16字节的基本格式信息
                if (!ReadFile(fileHandler.get(), &out->wf, 16, &bytesRead, NULL) || bytesRead != 16)
                {
                    throw yumo::exception_ex(
                        yumo::exception::type::FileReadError,
                        L"WAV文件读取失败，可能是文件损坏或其他错误");
                }
                // 如果有扩展信息（chunkSize > MINIMUM_FMT_SIZE），读取cbSize
                if (chunkSize > MINIMUM_FMT_SIZE)
                {
                    if (!ReadFile(fileHandler.get(), &out->wf.cbSize, 2, &bytesRead, NULL) || bytesRead != 2)
                    {
                        throw yumo::exception_ex(
                            yumo::exception::type::FileReadError,
                            L"WAV文件读取失败，可能是文件损坏或其他错误");
                    }
                    // 如果有额外扩展信息，跳过它们（目前）
                    if (chunkSize > COMMON_FMT_SIZE)
                    {
                        if (SetFilePointer(fileHandler.get(), chunkSize - COMMON_FMT_SIZE, NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER)
                        {
                            throw yumo::exception_ex(
                                yumo::exception::type::FileReadError,
                                L"WAV文件读取失败，可能是文件损坏或其他错误");
                        }
                    }
                }

                // 验证格式是否为支持的PCM格式
                ValidatePcmFormat(out->wf);
            }
            else if (chunkId == DATA_ID) // "data" 音频数据块
            {
                out->dataSize = chunkSize;
                // 分配内存并读取PCM数据到out->pcmData
                try
                {
                    out->pcmData = std::make_unique<char[]>(out->dataSize);
                }
                // 内存分配失败
                catch (const std::bad_alloc &)
                {
                    throw yumo::exception_ex(
                        yumo::exception::type::OutOfMemory,
                        L"内存分配失败，请检查WAV文件是否过大或受损");
                }
                // 读取PCM数据
                if (!ReadFile(fileHandler.get(), out->pcmData.get(), out->dataSize, &bytesRead, NULL) || bytesRead != out->dataSize)
                {
                    throw yumo::exception_ex(
                        yumo::exception::type::FileReadError,
                        L"WAV文件读取失败，可能是文件损坏或其他错误");
                }
                break; // 数据块读取完毕，退出循环
            }
            else
            {
                // 忽略其他块（如list、fact等），直接跳过
                // 检查 SetFilePointer 返回值以确保跳过成功
                if (SetFilePointer(fileHandler.get(), chunkSize, NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER)
                {
                    DWORD error = GetLastError();
                    // todo 利用这个错误码生成错误信息
                    (void)error; // error 可用于日志记录
                    throw yumo::exception_ex(
                        yumo::exception::type::FileReadError,
                        L"WAV文件读取失败，可能是文件损坏或其他错误");
                }
            }
        }
        out->valid = true; // 标记结构体有效
    }

    // 使用 Windows ACM 进行音频转换
    StandardWavInfo convertToStandard(const WavInfo &wavInfo)
    {
        // 检查WAV文件是否有效
        if (!wavInfo.valid)
        {
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"WAV文件格式无效");
        }

        const int sourceSampleRate = wavInfo.wf.nSamplesPerSec;
        const int sourceChannels = wavInfo.wf.nChannels;
        const int sourceBitsPerSample = wavInfo.wf.wBitsPerSample;
        const int targetSampleRate = 44100;
        const int targetChannels = 2; // 双声道
        const int targetBitsPerSample = 16;

        // 步骤1：设置源格式
        WAVEFORMATEX sourceFormat = {};
        sourceFormat.wFormatTag = WAVE_FORMAT_PCM;
        sourceFormat.nChannels = static_cast<WORD>(sourceChannels);
        sourceFormat.nSamplesPerSec = sourceSampleRate;
        sourceFormat.wBitsPerSample = sourceBitsPerSample;
        sourceFormat.nBlockAlign = sourceFormat.nChannels * sourceFormat.wBitsPerSample / 8;
        sourceFormat.nAvgBytesPerSec = sourceFormat.nSamplesPerSec * sourceFormat.nBlockAlign;
        sourceFormat.cbSize = 0;

        // 步骤2：设置目标格式
        WAVEFORMATEX targetFormat = {};
        targetFormat.wFormatTag = WAVE_FORMAT_PCM;
        targetFormat.nChannels = static_cast<WORD>(targetChannels);
        targetFormat.nSamplesPerSec = targetSampleRate;
        targetFormat.wBitsPerSample = targetBitsPerSample;
        targetFormat.nBlockAlign = targetFormat.nChannels * targetFormat.wBitsPerSample / 8;
        targetFormat.nAvgBytesPerSec = targetFormat.nSamplesPerSec * targetFormat.nBlockAlign;
        targetFormat.cbSize = 0;

        // 步骤3：打开ACM流
        HACMSTREAM hAcmStream = nullptr;
        MMRESULT mmResult = acmStreamOpen(
            &hAcmStream,
            NULL,          // 自动选择驱动程序
            &sourceFormat, // 源格式
            &targetFormat, // 目标格式
            NULL,          // 无滤波器
            0,             // 回调（同步模式）
            0,             // 实例数据
            0              // 同步操作
        );

        if (mmResult != MMSYSERR_NOERROR)
        {
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM流打开失败");
        }

        // 步骤4：计算目标缓冲区大小
        DWORD sourceSize = wavInfo.dataSize;
        DWORD destSize = 0;
        mmResult = acmStreamSize(hAcmStream, sourceSize, &destSize, ACM_STREAMSIZEF_SOURCE);
        if (mmResult != MMSYSERR_NOERROR)
        {
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"无法计算目标缓冲区大小");
        }

        // 步骤5：准备转换头
        std::unique_ptr<uint8_t[]> sourceBuffer(new uint8_t[sourceSize]);
        std::unique_ptr<uint8_t[]> destBuffer(new uint8_t[destSize]);

        ACMSTREAMHEADER streamHeader = {};
        streamHeader.cbStruct = sizeof(ACMSTREAMHEADER);
        streamHeader.pbSrc = sourceBuffer.get();
        streamHeader.cbSrcLength = sourceSize;
        streamHeader.pbDst = destBuffer.get();
        streamHeader.cbDstLength = destSize;

        mmResult = acmStreamPrepareHeader(hAcmStream, &streamHeader, 0);
        if (mmResult != MMSYSERR_NOERROR)
        {
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM头准备失败");
        }

        // 步骤6：复制源数据
        memcpy(sourceBuffer.get(), wavInfo.pcmData.get(), sourceSize);

        // 步骤7：执行转换
        streamHeader.cbSrcLength = sourceSize;
        streamHeader.cbDstLengthUsed = 0;
        mmResult = acmStreamConvert(hAcmStream, &streamHeader, ACM_STREAMCONVERTF_BLOCKALIGN);
        if (mmResult != MMSYSERR_NOERROR)
        {
            acmStreamUnprepareHeader(hAcmStream, &streamHeader, 0);
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"音频转换失败");
        }

        // 步骤8：清理ACM资源
        mmResult = acmStreamUnprepareHeader(hAcmStream, &streamHeader, 0);
        if (mmResult != MMSYSERR_NOERROR)
        {
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM头清理失败");
        }

        mmResult = acmStreamClose(hAcmStream, 0);
        if (mmResult != MMSYSERR_NOERROR)
        {
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM流关闭失败");
        }

        // 步骤9：将转换后的数据转换为 StandardWavInfo (int16_t 双声道交织)
        const size_t outputBytes = streamHeader.cbDstLengthUsed;
        const size_t outputSamples = outputBytes / sizeof(int16_t);
        StandardWavInfo result;
        result.resize(outputSamples);
        memcpy(result.data(), destBuffer.get(), outputBytes);

        return result;
    }
}

namespace yumo
{
    size_t preloadAudio(const wchar_t *filename, readySign *ready)
    {
        return AudioPool::getInstance().preloadAudio(filename, ready);
    }

    size_t addAudio(size_t preloadedId, float volume)
    {
        return AudioPool::getInstance().addAudio(preloadedId, volume);
    }

    void addAudio(const wchar_t *filename, float volume, size_t *instanceId, readySign *ready)
    {
        AudioPool::getInstance().addAudio(filename, volume, instanceId, ready);
    }

    void removePreloadedAudio(size_t preloadedId)
    {
        AudioPool::getInstance().removePreloadedAudio(preloadedId);
    }

    size_t getPreloadedCount()
    {
        return AudioPool::getInstance().getPreloadedCount();
    }

    size_t getPlayingCount()
    {
        return AudioPool::getInstance().getPlayingCount();
    }

    bool isPlaying(size_t instanceId)
    {
        return AudioPool::getInstance().isPlaying(instanceId);
    }

    void resetAll()
    {
        AudioPool::getInstance().resetAll();
    }

    void setVolume(size_t instanceId, float volume)
    {
        AudioPool::getInstance().setVolume(instanceId, volume);
    }

    float getVolume(size_t instanceId)
    {
        return AudioPool::getInstance().getVolume(instanceId);
    }

    bool stop(size_t instanceId)
    {
        return AudioPool::getInstance().stop(instanceId);
    }

    bool resume(size_t instanceId)
    {
        return AudioPool::getInstance().resume(instanceId);
    }

    bool setMuted(size_t instanceId, bool muted)
    {
        return AudioPool::getInstance().setMuted(instanceId, muted);
    }

    bool remove(size_t instanceId)
    {
        return AudioPool::getInstance().remove(instanceId);
    }
}
