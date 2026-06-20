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

namespace yumo
{
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
        StandardWavInfo data;  // 重采样后的音频数据（44.1kHz, 双声道, 16位）
    };

    /**
     * @brief 播放实例结构体
     * 
     * 每次 addAudio 创建一个播放实例，追踪独立的播放位置
     * 多个实例可以引用同一个预加载音频，实现同一音频的重复播放
     */
    struct PlayInstance {
        PreloadedAudio* source;  // 指向共享的预加载音频数据
        size_t position;              // 当前播放位置（样本索引）
        float volume;                // 音量（0.0-1.0）
        bool active;                 // 是否激活播放
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
         * @param ready 加载状态标记（调用时设为false，加载完成后变为true，使用atomic确保线程同步）
         * @return 预加载音频ID（用于后续 addAudio）
         */
        size_t preloadAudio(const wchar_t* filename, std::atomic<bool>& ready);

        /**
         * @brief 添加预加载音频到播放池并立即播放
         * 
         * 创建一个新的播放实例，与当前播放的音频混合
         * 
         * @param preloadedId preloadAudio 返回的预加载音频ID
         * @return 播放实例ID
         */
        size_t addAudio(size_t preloadedId);

        /**
         * @brief 添加音频文件到播放池并立即播放
         * 
         * 简化用法，内部自动完成异步预加载和添加播放
         * 会等待加载完成后再添加播放
         * 
         * @param filename WAV文件路径
         * @return 播放实例ID
         */
        size_t addAudio(const wchar_t* filename);

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
         * @brief 设置静音状态
         * 
         * @param muted true=静音，false=取消静音
         */
        void setMuted(bool muted);

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

        // 禁用拷贝构造和赋值
        AudioPool(const AudioPool&) = delete;
        AudioPool& operator=(const AudioPool&) = delete;

    private:
        AudioPool() = default;
        ~AudioPool() = default;

        std::vector<std::unique_ptr<PreloadedAudio>> preloadedAudios_;   // 预加载的音频数据（共享数据源）
        std::vector<PlayInstance> playInstances_;      // 当前播放的实例（独立位置追踪）
        mutable std::mutex mutex_;                     // 线程安全锁
        bool isPlaying_ = false;                        // 是否正在播放
        bool isMuted_ = false;                         // 是否静音（继续播放但跳过mix）
        bool isStopped_ = false;                       // 是否停止（挂起播放，不处理数据）
        HWAVEOUT hWaveOut_ = nullptr;                  // 音频设备句柄

        // 双缓冲常量
        static const size_t BUFFER_COUNT = 2;           // 缓冲区数量

        // 混合音频回调函数
        static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
        
        // 混合一小段音频
        void mixAudioChunk(int16_t* output, size_t chunkSize);

        // 准备音频设备
        void ensureDeviceOpen();
    };

    /**
     * @brief 将WAV文件信息转换为标准格式
     *
     * @param[in] wavInfo 包含WAV文件格式信息和音频数据的结构体
     * @return StandardWavInfo 标准格式的WAV文件信息，包含16位整数音频数据
     *
     */
    StandardWavInfo convertToStandard(const WavInfo &wavInfo);

    /**
     * @brief 从磁盘加载WAV文件，解析其格式块和数据块，填充WavInfo结构体
     *
     * @param[in] filename WAV文件的路径（宽字符字符串）
     * @param[out] out 指向WavInfo结构体的指针，用于存储解析后的文件信息
     * @return 无返回值，如果发生错误，将抛出异常
     *
     * @note 读取中发生任何异常，函数都将抛出异常，且out结构体的内容不保证有效。
     */
    void loadWav(const wchar_t *filename, WavInfo *out);
}