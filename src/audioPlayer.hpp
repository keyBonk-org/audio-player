/**
 * @file        audioPlayer.hpp
 * @brief       本文件是Yumo Audio库的头文件， 提供音频播放相关的类和函数声明。
 * 通过引入该头文件，可以使用Yumo Audio库进行音频文件的预加载、播放、控制音量、
 * 静音、停止等操作。
 * @details     暂无
 * @author      xiaoditx <552333302@qq.com>
 * @version     0.2.0
 * @copyright   开源，遵循 WTFPL 协议
 */
#ifndef AUDIO_PLAYER_HPP
#define AUDIO_PLAYER_HPP
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>

namespace yumo
{
    /**
     * @brief 原子类型封装类。
     * 用于封装 std::atomic，提供更方便的操作接口；
     * 支持隐式转换、赋值、比较等操作；
     * 内部使用 std::atomic，保证线程安全。
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
        // 拷贝构造
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

        // 隐式转换为T
        operator T() const
        {
            return value_.load(std::memory_order_seq_cst);
        }

        // 与 T 的比较
        bool operator==(T other) const
        {
            return value_.load(std::memory_order_seq_cst) == other;
        }

        bool operator!=(T other) const
        {
            return value_.load(std::memory_order_seq_cst) != other;
        }

        // 支持 load / store 以便一些模板使用（虽然不是很推荐在库外使用这个类）
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

    // 相关typedef
    using readySign = atomic<bool>;   // 完成标记
    using switchSign = atomic<bool>;  // 开关标记
    using volumeSign = atomic<float>; // 音量标记

    /**
     * @brief 全集音频控制信号类
     *
     * 用于控制全局音频状态，支持直接赋值操作
     *
     * todo 将其应用到单个音频上去
     */
    class audioSign
    {
    public:
        switchSign mute{false};  // 静音
        switchSign stop{false};  // 停止（挂起）
        volumeSign volume{1.0f}; // 音量（0.0-1.0）
    };

    /**
     * @brief 播放实例类
     *
     * 用于表示一个音频播放实例，包含播放位置、音量、激活状态、停止状态和静音状态等信息。
     * 通过该类可以控制和查询音频播放实例的状态。
     *
     * todo 提供一个成员记录时长
     */
    class audioInstance
    {
    public:
        // 代理类，将指针隐藏为常规变量
        template <typename T>
        class proxy
        {
        private:
            T *ptr_;
            std::shared_ptr<std::mutex> mutex_;

        public:
            // todo 互斥锁nullptr
            proxy() : ptr_(nullptr), mutex_(nullptr) {}
            proxy(T &value) : ptr_(&value), mutex_(std::make_shared<std::mutex>()) {}
            proxy(const proxy &other) : ptr_(other.ptr_), mutex_(other.mutex_) {}
            proxy &operator=(const proxy &other) = delete;
            operator T() const
            {
                if (mutex_)
                    std::lock_guard<std::mutex> lock(*mutex_);
                return ptr_ ? *ptr_ : T();
            }
            proxy &operator=(T value)
            {
                if (mutex_)
                    std::lock_guard<std::mutex> lock(*mutex_);
                if (ptr_)
                    *ptr_ = value;
                return *this;
            }
            friend class audioInstance;
        };
        const size_t instanceId; // 播放实例ID（按值传递）
        // todo 改成秒为单位方便用户使用
        proxy<size_t> position; // 播放位置（采样点）
        proxy<float> volume;    // 音量（0.0-1.0）
        proxy<bool> active;     // 是否激活播放
        proxy<bool> stopped;    // 是否停止（挂起）
        proxy<bool> muted;      // 是否静音（跳过混音但位置继续推进）
        audioInstance() : instanceId(0), position(), volume(), active(), stopped(), muted() {}
        audioInstance(size_t id, size_t &pos, float &vol, bool &active, bool &stop, bool &mute)
            : instanceId(id), position(pos), volume(vol), active(active), stopped(stop), muted(mute) {}
        audioInstance(const audioInstance &other)
            : instanceId(other.instanceId), position(other.position), volume(other.volume), active(other.active), stopped(other.stopped), muted(other.muted) {}
        // 用户层面上说，proxy = proxy 操作像是一个变量值赋到另一个变量（other隐式转换后触发self赋值操作）
        // instance = instance 操作像是一个播放实例赋值另一个播放实例，也就是proxy所有权的转移
        // 因此不给proxy重载赋值运算符，避免用户误以为是赋值操作
        audioInstance &operator=(const audioInstance &other)
        {
            if (this == &other)
                return *this;
            const_cast<size_t &>(instanceId) = other.instanceId;
            position.ptr_ = other.position.ptr_;
            position.mutex_ = other.position.mutex_;
            volume.ptr_ = other.volume.ptr_;
            volume.mutex_ = other.volume.mutex_;
            active.ptr_ = other.active.ptr_;
            active.mutex_ = other.active.mutex_;
            stopped.ptr_ = other.stopped.ptr_;
            stopped.mutex_ = other.stopped.mutex_;
            muted.ptr_ = other.muted.ptr_;
            muted.mutex_ = other.muted.mutex_;
            return *this;
        }
        bool isPlaying() const;
    };

    /**
     * @brief 播放完成回调类型
     *
     * 当播放实例被自动回收时调用，通知用户该实例已失效
     *
     * @param instanceId 被回收的播放实例ID
     */
    using PlaybackFinishedCallback = std::function<void(size_t instanceId)>;

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
     * @param[in] filename 音频文件路径（目前支持WAV、MP3格式）
     * @param[out] ready 可选的加载状态标记，按地址传递，调用时自动被设为false，加载完成后变为true
     * @return 预加载音频ID
     */
    size_t preloadAudio(const wchar_t *filename, readySign *ready = nullptr);
    /**
     * @brief 添加已预加载的音频到播放池并立即播放
     * @param[in] preloadedId 预加载音频ID
     * @param[in] volume 音量，最小0.0，最大1.0，默认为 1.0
     * @return 播放实例（包含instanceId）
     */
    audioInstance addAudio(size_t preloadedId, float volume = 1.0f);
    /**
     * @brief 通过实例ID重新获取播放实例
     *
     * 如果实例已被回收或不存在，返回无效的audioInstance（instanceId为0）
     *
     * @param[in] instanceId 播放实例ID
     * @return 播放实例，如果无效则instanceId为0
     */
    audioInstance regain(size_t instanceId);
    /**
     * @brief 注册播放完成回调
     *
     * 当播放实例被自动回收时调用此回调
     *
     * @param[in] callback 回调函数，参数为被回收的实例ID
     */
    void registerPlaybackFinishedCallback(PlaybackFinishedCallback callback);
    /**
     * @brief 注销播放完成回调
     */
    void unregisterPlaybackFinishedCallback();
    /**
     * @brief 添加未预加载的音频文件到播放池并立即播放
     *
     * 简化用法，内部自动完成异步预加载和添加播放。
     * 播放完成后自动移除预加载对象。
     *
     * @param[in] filename 音频文件路径（目前支持WAV、MP3格式）
     * @param[in] volume 音量，最小0.0，最大1.0，默认为 1.0
     * @param[out] instance 可选的播放实例输出，音频播放开始后写入
     * @param[out] ready 可选的加载状态标记，按地址传递，调用时自动被设为false，加载完成后变为true
     */
    void addAudio(const wchar_t *filename, float volume = 1.0f, yumo::audioInstance *instance = nullptr, yumo::readySign *ready = nullptr);
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
     * @brief 重置所有播放实例的位置到开头
     */
    void resetAll();
    /**
     * @brief 从播放池中移除指定播放实例
     *
     * @param[in] instanceId 播放实例ID
     * @throws yumo::exception 无效的播放实例ID
     */
    void remove(size_t instanceId);

} // namespace yumo

#endif // AUDIO_PLAYER_HPP
