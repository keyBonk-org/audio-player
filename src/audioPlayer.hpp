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
    template <typename T>
    class atomic
    {
    private:
        std::atomic<T> value_;

    public:
        atomic() = default;
        atomic(T val) : value_(val) {}

        atomic &operator=(T val)
        {
            value_.store(val, std::memory_order_relaxed);
            return *this;
        }

        operator T() const
        {
            return value_.load(std::memory_order_relaxed);
        }

        bool operator==(T other) const
        {
            return value_.load(std::memory_order_relaxed) == other;
        }

        bool operator!=(T other) const
        {
            return value_.load(std::memory_order_relaxed) != other;
        }

        T load(std::memory_order order = std::memory_order_relaxed) const
        {
            return value_.load(order);
        }

        void store(T val, std::memory_order order = std::memory_order_relaxed)
        {
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
    class audioSign
    {
    public:
        switchSign mute{false};  // 全局静音
        switchSign stop{false};  // 全局停止（挂起）
        volumeSign volume{1.0f}; // 全局音量（0.0-1.0）
    };

    /**
     * @brief 全局音频控制信号实例
     *
     * 用于控制全局音频状态，支持直接赋值操作
     *
     * 未来可扩展为每个音频实例配一个
     *
     */
    inline yumo::audioSign global;

    // ===== 全局函数包装层 =====

    size_t preloadAudio(const wchar_t *filename, readySign *ready = nullptr);
    size_t addAudio(size_t preloadedId, float volume = 1.0f);
    void addAudio(const wchar_t *filename, float volume = 1.0f, size_t *instanceId = nullptr, readySign *ready = nullptr);
    void removePreloadedAudio(size_t preloadedId);
    size_t getPreloadedCount();
    size_t getPlayingCount();
    bool isPlaying(size_t instanceId);
    void stopAll();
    void resume();
    void setGlobalMute(bool muted);
    void resetAll();
    void setVolume(size_t instanceId, float volume);
    float getVolume(size_t instanceId);
    bool stop(size_t instanceId);
    bool resume(size_t instanceId);
    bool setMuted(size_t instanceId, bool muted);
    bool remove(size_t instanceId);

} // namespace yumo