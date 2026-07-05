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
#include <queue>
#include <atomic>

namespace yumo
{
    template<typename T>
    class atomic {
    private:
        std::atomic<T> value_;
    public:
        atomic() = default;
        atomic(T val) : value_(val) {}
        
        atomic& operator=(T val) {
            value_.store(val, std::memory_order_relaxed);
            return *this;
        }
        
        operator T() const {
            return value_.load(std::memory_order_relaxed);
        }
        
        bool operator==(T other) const {
            return value_.load(std::memory_order_relaxed) == other;
        }
        
        bool operator!=(T other) const {
            return value_.load(std::memory_order_relaxed) != other;
        }
        
        T load(std::memory_order order = std::memory_order_relaxed) const {
            return value_.load(order);
        }
        
        void store(T val, std::memory_order order = std::memory_order_relaxed) {
            value_.store(val, order);
        }
    };

    using readySign = atomic<bool>;
    using switchSign = atomic<bool>;
    using volumeSign = atomic<float>;

    /**
     * @brief 音频控制信号类
     *
     * 用于控制全局音频状态，支持直接赋值操作
     * 未来可扩展为每个音频实例配一个
     */
    class audioSign {
    public:
        switchSign mute{false};   // 全局静音
        switchSign stop{false};   // 全局停止（挂起）
        volumeSign volume{1.0f};  // 全局音量（0.0-1.0）
    };

    inline audioSign global;

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
    struct PreloadedAudio {
        StandardWavInfo data;        // 重采样后的音频数据（44.1kHz, 双声道, 16位）
        bool markedForRemoval = false;  // 标记为待删除（播放结束后清理）
    };

    /**
     * @brief 播放实例结构体
     *
     * 每次 addAudio 创建一个播放实例，追踪独立的播放位置
     * 多个实例可以引用同一个预加载音频，实现同一音频的重复播放
     */
    struct PlayInstance {
        PreloadedAudio* source;      // 指向共享的预加载音频数据
        size_t position;             // 当前播放位置（样本索引）
        float volume;                // 音量（0.0-1.0）
        bool active;                 // 是否激活播放
        bool stopped;                // 是否暂停（与active区别：stopped时位置不推进）
        bool muted;                  // 是否静音（跳过混音但位置继续推进）
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
        static AudioPool& getInstance();

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
        size_t preloadAudio(const wchar_t* filename, readySign* ready = nullptr);

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
        void addAudio(const wchar_t* filename, float volume = 1.0f, size_t* instanceId = nullptr, readySign* ready = nullptr);

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
         * @brief 停止所有播放（挂起）
         * 
         * 挂起播放，不处理数据，等待恢复
         * 调用 resume() 可恢复播放
         */
        void stopAll();

        /**
         * @brief 恢复播放
         * 
         * 取消停止和静音状态，继续播放
         */
        void resume();

        /**
         * @brief 设置全局静音状态
         *
         * @param muted true=静音，false=取消静音
         */
        void setGlobalMute(bool muted);

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
        AudioPool(const AudioPool&) = delete;
        AudioPool& operator=(const AudioPool&) = delete;

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

        void mixAudioChunk(int16_t* output, size_t chunkSize);

        void ensureDeviceOpen();
    };

    // ===== 全局函数包装层 =====

    inline size_t preloadAudio(const wchar_t* filename, readySign* ready = nullptr) {
        return AudioPool::getInstance().preloadAudio(filename, ready);
    }

    inline size_t addAudio(size_t preloadedId, float volume = 1.0f) {
        return AudioPool::getInstance().addAudio(preloadedId, volume);
    }

    inline void addAudio(const wchar_t* filename, float volume = 1.0f, size_t* instanceId = nullptr, readySign* ready = nullptr) {
        AudioPool::getInstance().addAudio(filename, volume, instanceId, ready);
    }

    inline void removePreloadedAudio(size_t preloadedId) {
        AudioPool::getInstance().removePreloadedAudio(preloadedId);
    }

    inline size_t getPreloadedCount() {
        return AudioPool::getInstance().getPreloadedCount();
    }

    inline size_t getPlayingCount() {
        return AudioPool::getInstance().getPlayingCount();
    }

    inline bool isPlaying(size_t instanceId) {
        return AudioPool::getInstance().isPlaying(instanceId);
    }

    inline void stopAll() {
        AudioPool::getInstance().stopAll();
    }

    inline void resume() {
        AudioPool::getInstance().resume();
    }

    inline void setGlobalMute(bool muted) {
        AudioPool::getInstance().setGlobalMute(muted);
    }

    inline void resetAll() {
        AudioPool::getInstance().resetAll();
    }

    inline void setVolume(size_t instanceId, float volume) {
        AudioPool::getInstance().setVolume(instanceId, volume);
    }

    inline float getVolume(size_t instanceId) {
        return AudioPool::getInstance().getVolume(instanceId);
    }

    inline bool stop(size_t instanceId) {
        return AudioPool::getInstance().stop(instanceId);
    }

    inline bool resume(size_t instanceId) {
        return AudioPool::getInstance().resume(instanceId);
    }

    inline bool setMuted(size_t instanceId, bool muted) {
        return AudioPool::getInstance().setMuted(instanceId, muted);
    }

    inline bool remove(size_t instanceId) {
        return AudioPool::getInstance().remove(instanceId);
    }

} // namespace yumo