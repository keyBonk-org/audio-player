#ifndef AUDIO_PLAYER_HPP
#define AUDIO_PLAYER_HPP
#include <atomic>

namespace yumo
{
    /**
     * @brief 原子类型封装类
     * 用于封装 std::atomic，提供更方便的操作接口
     * 支持隐式转换、赋值、比较等操作
     * 内部使用 std::atomic，保证线程安全
     *
     * @tparam T 原子类型
     */
    template <typename T>
    class atomic
    {
    private:
        std::atomic<T> value_;

    public:
        // 构造函数
        atomic() = default;
        atomic(T val) : value_(val) {}
        // 拷贝构造（手动实现）
        atomic(const atomic &other) : value_(other.load()) {}
        // 拷贝赋值
        atomic &operator=(const atomic &other)
        {
            if (this != &other)
                value_.store(other.load(), std::memory_order_seq_cst);
            return *this;
        }
        // 从 T 赋值
        atomic &operator=(T val)
        {
            value_.store(val, std::memory_order_seq_cst);
            return *this;
        }

        // 隐式转换为 T（内存序 seq_cst）
        operator T() const
        {
            return value_.load(std::memory_order_seq_cst);
        }

        // 与 T 的比较（原有，内存序改为 seq_cst）
        bool operator==(T other) const
        {
            return value_.load(std::memory_order_seq_cst) == other;
        }

        bool operator!=(T other) const
        {
            return value_.load(std::memory_order_seq_cst) != other;
        }

        // load / store（默认内存序 seq_cst）
        T load(std::memory_order order = std::memory_order_seq_cst) const
        {
            return value_.load(order);
        }

        void store(T val, std::memory_order order = std::memory_order_seq_cst)
        {
            value_.store(val, order);
        }
    };

    // 两个 yumo::atomic 之间的比较
    template <typename T>
    bool operator==(const atomic<T> &a, const atomic<T> &b)
    {
        return a.load() == b.load();
    }

    template <typename T>
    bool operator!=(const atomic<T> &a, const atomic<T> &b)
    {
        return a.load() != b.load();
    }

    // yumo::atomic 与 std::atomic 之间的比较
    template <typename T>
    bool operator==(const atomic<T> &a, const std::atomic<T> &b)
    {
        return a.load() == b.load();
    }

    template <typename T>
    bool operator!=(const atomic<T> &a, const std::atomic<T> &b)
    {
        return a.load() != b.load();
    }

    template <typename T>
    bool operator==(const std::atomic<T> &a, const atomic<T> &b)
    {
        return a.load() == b.load();
    }

    template <typename T>
    bool operator!=(const std::atomic<T> &a, const atomic<T> &b)
    {
        return a.load() != b.load();
    }

    using readySign = atomic<bool>;
    using switchSign = atomic<bool>;
    using volumeSign = atomic<float>;

    /**
     * @brief 音频控制信号类
     *
     * 用于控制音频状态，支持直接赋值操作
     */
    class audioSign
    {
    public:
        switchSign mute{false};  // 静音
        switchSign stop{false};  // 停止（挂起）
        volumeSign volume{1.0f}; // 音量（0.0-1.0）
    };

    /**
     * @brief 全局音频控制信号实例
     *
     * 用于控制全局音频状态，支持直接赋值操作
     */
    inline yumo::audioSign global;

    // ===== 全局函数包装层 =====

    /**
     * @brief 预加载音频
     *
     * @param[in] filename 音频文件路径
     * @param[out] ready 可选的加载状态标记，按地址传递，调用时自动被设为false，加载完成后变为true
     * @return 预加载音频ID
     */
    size_t preloadAudio(const wchar_t *filename, readySign *ready = nullptr);
    /**
     * @brief 添加已预加载的音频到播放池并立即播放
     * @param[in] preloadedId 预加载音频ID
     * @param[in] volume 音量，最小0.0，最大1.0，默认为 1.0
     * @return 播放实例ID
     */
    size_t addAudio(size_t preloadedId, float volume = 1.0f);
    /**
     * @brief 添加未预加载的音频文件到播放池并立即播放
     *
     * 简化用法，内部自动完成异步预加载和添加播放。
     * 播放完成后自动移除预加载对象。
     *
     * @param[in] filename WAV文件路径
     * @param[in] volume 音量，最小0.0，最大1.0，默认为 1.0
     * @param[out] instanceId 可选的播放实例ID输出，音频播放开始后写入
     * @param[out] ready 可选的加载状态标记，按地址传递，调用时自动被设为false，加载完成后变为true
     */
    void addAudio(const wchar_t *filename, float volume = 1.0f, size_t *instanceId = nullptr, readySign *ready = nullptr);
    /**
     * @brief 从预加载队列中移除预加载音频对象
     *
     * @param[in] preloadedId preloadAudio 返回的预加载音频ID
     * @note 如果该预加载对象正在被播放实例引用，播放会继续直到结束
     */
    void removePreloadedAudio(size_t preloadedId);
    /**
     * @brief 获取预加载音频数量
     *
     * @return 预加载音频数量
     */
    size_t getPreloadedCount();
    /**
     * @brief 获取正在播放的音频数量
     *
     * @return 正在播放的音频数量
     */
    size_t getPlayingCount();
    /**
     * @brief 检查指定播放实例是否正在播放
     *
     * @param[in] instanceId 播放实例ID
     * @return 如果正在播放返回true，否则返回false
     */
    bool isPlaying(size_t instanceId);
    /**
     * @brief 重置所有播放实例的位置到开头
     */
    void resetAll();
    /**
     * @brief 设置指定播放实例的音量
     * @param[in] instanceId 播放实例ID
     * @param[in] volume 音量（0.0-1.0）
     */
    void setVolume(size_t instanceId, float volume);
    /**
     * @brief 获取指定播放实例的音量
     * @param[in] instanceId 播放实例ID
     * @return 音量（0.0-1.0）
     */
    float getVolume(size_t instanceId);
    /**
     * @brief 停止指定播放实例
     *
     * @param[in] instanceId 播放实例ID
     * @return true=成功，false=无效ID
     */
    bool stop(size_t instanceId);
    /**
     * @brief 恢复指定播放实例
     *
     * @param[in] instanceId 播放实例ID
     * @return true=成功，false=无效ID
     */
    bool resume(size_t instanceId);
    /**
     * @brief 设置指定播放实例的静音状态
     *
     * @param[in] instanceId 播放实例ID
     * @param[in] muted true=静音，false=取消静音
     * @return true=成功，false=无效ID
     */
    bool setMuted(size_t instanceId, bool muted);
    /**
     * @brief 从播放池中移除指定播放实例
     *
     * @param[in] instanceId 播放实例ID
     * @return true=成功，false=无效ID
     */
    bool remove(size_t instanceId);

} // namespace yumo

#endif // AUDIO_PLAYER_HPP