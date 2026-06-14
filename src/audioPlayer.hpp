#include <windows.h>
#include <mmreg.h>
#include <stdio.h>
#include <memory>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <map>
#include <mutex>

// libsamplerate 库
#include "libsamplerate/include/samplerate.h"

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
     * @brief 音频池类 - 单例模式
     * 
     * 管理多个音频文件的加载、重采样、播放位置和混合播放
     */
    class AudioPool
    {
    public:
        /**
         * @brief 获取单例实例
         */
        static AudioPool& getInstance();

        /**
         * @brief 添加音频到音频池
         * 
         * @param filename WAV文件路径
         * @return 音频ID（用于后续引用）
         */
        size_t addAudio(const wchar_t* filename);

        /**
         * @brief 获取音频数量
         */
        size_t getAudioCount() const;

        /**
         * @brief 检查指定ID的音频是否正在播放
         */
        bool isAudioPlaying(size_t audioId) const;

        /**
         * @brief 开始播放所有音频（混合播放）
         */
        void playAll();

        /**
         * @brief 停止播放
         */
        void stop();

        /**
         * @brief 重置所有音频的播放位置到开头
         */
        void resetAll();

        /**
         * @brief 设置音频音量
         * 
         * @param audioId 音频ID
         * @param volume 音量（0.0-1.0）
         */
        void setVolume(size_t audioId, float volume);

        /**
         * @brief 获取音频音量
         */
        float getVolume(size_t audioId) const;

        // 禁用拷贝构造和赋值
        AudioPool(const AudioPool&) = delete;
        AudioPool& operator=(const AudioPool&) = delete;

    private:
        AudioPool() = default;
        ~AudioPool() = default;

        // 音频项结构体
        struct AudioItem {
            StandardWavInfo data;       // 重采样后的音频数据（44.1kHz, 双声道, 16位）
            size_t position;            // 当前播放位置（样本索引）
            float volume;               // 音量（0.0-1.0）
            bool active;                // 是否激活播放
        };

        std::vector<AudioItem> audioItems_;  // 音频池
        mutable std::mutex mutex_;           // 线程安全锁
        bool isPlaying_ = false;             // 是否正在播放

        // 混合音频回调函数
        static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
        
        // 混合一小段音频
        void mixAudioChunk(int16_t* output, size_t chunkSize);
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
     * @brief 使用 waveOut API 播放标准格式音频
     *
     * @param[in] audioData 标准格式音频数据（44.1kHz, 双声道, 16位）
     */
    void playStandard(const StandardWavInfo &audioData);

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