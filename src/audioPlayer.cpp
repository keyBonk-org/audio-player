#ifndef UNICODE
#define UNICODE
#endif

#include "audioPlayer.hpp"
#include "yumo_except.hpp"
#include <iostream>
#include <cassert>
#include <functional>
#include <thread>
#include <atomic>

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
    const DWORD RIFF_ID = 0x46464952; // "RIFF" 的小端序整数值
    const DWORD WAVE_ID = 0x45564157; // "WAVE" 的小端序整数值
    const DWORD FMT_ID = 0x20746D66;  // "fmt " 的小端序整数值（注意空格）
    const DWORD DATA_ID = 0x61746164; // "data" 的小端序整数值
    const size_t MINIMUM_FMT_SIZE = 16; // 最小fmt块大小（包含所有必要字段）
    const size_t MAXIMUM_FMT_SIZE = 24; // 最大fmt块大小（包含所有可选字段）
    const size_t COMMON_FMT_SIZE = sizeof(WAVEFORMATEX); // fmt块大小（包含所有字段）
}

namespace yumo
{
    // 音频缓冲区大小（约100ms的音频）
    // 计算：采样率 * 声道数 * 时长(秒) = 44100 * 2 * 0.1 = 8820 个样本
    const size_t AUDIO_CHUNK_SIZE = static_cast<size_t>(44100 * 2 * 0.1);

    // AudioPool 单例实现
    AudioPool& AudioPool::getInstance()
    {
        static AudioPool instance;
        return instance;
    }

    // 预加载音频文件（异步）
    size_t AudioPool::preloadAudio(const wchar_t* filename, std::atomic<bool>& ready)
    {
        // 标记加载状态为未完成
        ready = false;

        // 预留一个位置，获取 ID
        size_t preloadedId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            preloadedAudios_.push_back(std::make_unique<PreloadedAudio>());  // 预留空位置
            preloadedId = preloadedAudios_.size() - 1;
        }

        // 创建后台加载线程
        std::thread loadThread([this, filename, preloadedId, &ready]() {
            try {
                // 加载WAV文件（不持有锁，避免阻塞播放）
                WavInfo wavInfo;
                loadWav(filename, &wavInfo);

                // 重采样到标准格式
                StandardWavInfo standardData = convertToStandard(wavInfo);

                // 填充预留的位置
            {
                std::lock_guard<std::mutex> lock(mutex_);
                preloadedAudios_[preloadedId]->data = std::move(standardData);
            }

                // 标记加载完成（atomic 确保线程同步）
                ready = true;
            } catch (...) {
                // 加载失败，标记完成（但数据为空）
                ready = true;
            }
        });

        // 分离线程，让它在后台运行
        loadThread.detach();

        return preloadedId;
    }

    // 添加预加载音频并立即播放
    size_t AudioPool::addAudio(size_t preloadedId)
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
        instance.volume = 1.0f;
        instance.active = true;

        playInstances_.push_back(instance);
        size_t instanceId = playInstances_.size() - 1;

        // 如果设备未打开，则打开
        if (!hWaveOut_) {
            lock.unlock();
            ensureDeviceOpen();
        }

        return instanceId;
    }

    // 添加音频文件并立即播放（简化用法）
    size_t AudioPool::addAudio(const wchar_t* filename)
    {
        // 使用智能指针确保 ready 的生命周期覆盖后台线程
        auto ready = std::make_shared<std::atomic<bool>>(false);
        size_t preloadedId = preloadAudio(filename, *ready);
        
        // 在后台线程等待加载完成并播放
        std::thread([this, preloadedId, ready]() {
            // 等待加载完成
            while (!ready->load()) {
                Sleep(10);
            }
            
            // 加载完成后检查数据是否有效
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (preloadedId >= preloadedAudios_.size() || preloadedAudios_[preloadedId]->data.empty()) {
                    // 加载失败
                    return;
                }
            }
            
            // 添加播放
            try {
                addAudio(preloadedId);
            } catch (...) {
                // 忽略播放失败
            }
        }).detach();
        
        // 返回预加载ID作为标识（用户可用于后续控制）
        return preloadedId;
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
        if (instanceId >= playInstances_.size())
            return false;
        const auto& inst = playInstances_[instanceId];
        return inst.active && inst.source && inst.position < inst.source->data.size();
    }

    // 混合一小段音频
    void AudioPool::mixAudioChunk(int16_t* output, size_t chunkSize)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 如果停止（挂起），输出静默但不推进position
        if (isStopped_) {
            memset(output, 0, chunkSize * sizeof(int16_t));
            return;
        }

        // 如果静音，跳过mix处理但继续推进position（性能优化）
        if (isMuted_) {
            // 推进所有激活播放实例的位置
            for (auto& inst : playInstances_) {
                if (!inst.active || !inst.source || inst.position >= inst.source->data.size())
                    continue;
                size_t remaining = inst.source->data.size() - inst.position;
                size_t samplesToAdvance = std::min(chunkSize, remaining);
                inst.position += samplesToAdvance;
            }
            // 输出静默
            memset(output, 0, chunkSize * sizeof(int16_t));
            return;
        }

        // 清空输出缓冲区
        memset(output, 0, chunkSize * sizeof(int16_t));

        // 混合所有激活的播放实例
        for (auto& inst : playInstances_) {
            if (!inst.active || !inst.source || inst.position >= inst.source->data.size())
                continue;

            const auto& data = inst.source->data;
            size_t remaining = data.size() - inst.position;
            size_t samplesToCopy = std::min(chunkSize, remaining);

            for (size_t i = 0; i < samplesToCopy; ++i) {
                // 应用音量并混合
                int32_t mixed = static_cast<int32_t>(output[i]) +
                    static_cast<int32_t>(data[inst.position + i] * inst.volume);

                // 防止溢出
                if (mixed > INT16_MAX) mixed = INT16_MAX;
                if (mixed < INT16_MIN) mixed = INT16_MIN;

                output[i] = static_cast<int16_t>(mixed);
            }

            // 更新播放位置
            inst.position += samplesToCopy;
        }
    }

    // 音频播放回调
    void CALLBACK AudioPool::waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (uMsg != WOM_DONE)
            return;

        WAVEHDR* pHeader = reinterpret_cast<WAVEHDR*>(dwParam1);
        AudioPool* pPool = reinterpret_cast<AudioPool*>(dwInstance);

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
            if (!pPool->isPlaying_) {
                return; // 已停止播放
            }
            // 如果有激活的实例还没播完，就继续
            for (const auto& inst : pPool->playInstances_) {
                if (inst.active && inst.source && inst.position < inst.source->data.size()) {
                    allFinished = false;
                    break;
                }
            }
        }

        if (allFinished) {
            // 所有音频播放完毕，关闭设备
            std::lock_guard<std::mutex> lock(pPool->mutex_);
            pPool->isPlaying_ = false;
            waveOutClose(hwo);
            pPool->hWaveOut_ = nullptr;
            return;
        }

        // 准备新的数据
        int16_t* pData = new int16_t[AUDIO_CHUNK_SIZE];
        pPool->mixAudioChunk(pData, AUDIO_CHUNK_SIZE);

        WAVEHDR* pNewHeader = new WAVEHDR;
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
        for (auto& inst : playInstances_) {
            if (inst.active && inst.source && inst.position < inst.source->data.size()) {
                inst.position = 0;
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

        if (mmResult != MMSYSERR_NOERROR) {
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"波形音频设备打开失败");
        }

        lock.lock();
        hWaveOut_ = hWaveOut;
        isPlaying_ = true;
        isMuted_ = false;
        lock.unlock();

        // 预先准备多个缓冲区（双缓冲）
        for (size_t i = 0; i < BUFFER_COUNT; ++i) {
            int16_t* pData = new int16_t[AUDIO_CHUNK_SIZE];
            mixAudioChunk(pData, AUDIO_CHUNK_SIZE);

            WAVEHDR* pHeader = new WAVEHDR;
            memset(pHeader, 0, sizeof(WAVEHDR));
            pHeader->lpData = reinterpret_cast<LPSTR>(pData);
            pHeader->dwBufferLength = static_cast<DWORD>(AUDIO_CHUNK_SIZE * sizeof(int16_t));

            waveOutPrepareHeader(hWaveOut, pHeader, sizeof(WAVEHDR));
            waveOutWrite(hWaveOut, pHeader, sizeof(WAVEHDR));
        }
    }

    // 停止所有播放（挂起）
    void AudioPool::stopAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 设置停止标志，挂起播放
        isStopped_ = true;
    }

    // 恢复播放
    void AudioPool::resume()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isStopped_ = false;
        isMuted_ = false;
    }

    // 设置静音状态
    void AudioPool::setMuted(bool muted)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isMuted_ = muted;
    }

    // 重置所有播放实例的位置到开头
    void AudioPool::resetAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& inst : playInstances_) {
            inst.position = 0;
        }
    }

    // 设置播放实例音量
    void AudioPool::setVolume(size_t instanceId, float volume)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instanceId >= playInstances_.size())
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"无效的播放实例ID");
        
        playInstances_[instanceId].volume = std::max(0.0f, std::min(1.0f, volume));
    }

    // 获取播放实例音量
    float AudioPool::getVolume(size_t instanceId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instanceId >= playInstances_.size())
            throw yumo::exception_ex(yumo::exception::type::InvalidInput, L"无效的播放实例ID");
        
        return playInstances_[instanceId].volume;
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
            NULL,                   // 自动选择驱动程序
            &sourceFormat,          // 源格式
            &targetFormat,          // 目标格式
            NULL,                   // 无滤波器
            0,                      // 回调（同步模式）
            0,                      // 实例数据
            0                       // 同步操作
        );

        if (mmResult != MMSYSERR_NOERROR) {
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM流打开失败");
        }

        // 步骤4：计算目标缓冲区大小
        DWORD sourceSize = wavInfo.dataSize;
        DWORD destSize = 0;
        mmResult = acmStreamSize(hAcmStream, sourceSize, &destSize, ACM_STREAMSIZEF_SOURCE);
        if (mmResult != MMSYSERR_NOERROR) {
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
        if (mmResult != MMSYSERR_NOERROR) {
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM头准备失败");
        }

        // 步骤6：复制源数据
        memcpy(sourceBuffer.get(), wavInfo.pcmData.get(), sourceSize);

        // 步骤7：执行转换
        streamHeader.cbSrcLength = sourceSize;
        streamHeader.cbDstLengthUsed = 0;
        mmResult = acmStreamConvert(hAcmStream, &streamHeader, ACM_STREAMCONVERTF_BLOCKALIGN);
        if (mmResult != MMSYSERR_NOERROR) {
            acmStreamUnprepareHeader(hAcmStream, &streamHeader, 0);
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"音频转换失败");
        }

        // 步骤8：清理ACM资源
        mmResult = acmStreamUnprepareHeader(hAcmStream, &streamHeader, 0);
        if (mmResult != MMSYSERR_NOERROR) {
            acmStreamClose(hAcmStream, 0);
            throw yumo::exception_ex(yumo::exception::type::UnknownError, L"ACM头清理失败");
        }

        mmResult = acmStreamClose(hAcmStream, 0);
        if (mmResult != MMSYSERR_NOERROR) {
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