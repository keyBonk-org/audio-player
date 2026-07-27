#ifndef YUMO_EXCEPT_HPP
#define YUMO_EXCEPT_HPP

#include <string>

/**
 * @file yumo_except.hpp
 * @brief 定义了yumo库项目中使用的自定义异常类
 * 这个文件包含了一个简单的异常类，用于在项目中抛出和捕获特定类型的错误。通过使用这个自定义异常类，可以更清晰地表达错误的类型和原因，便于调试和维护。
 * @author yumo XiaoDi
 */

namespace yumo
{
    /**
     * @brief yumo库标准异常类
     *
     * 这个类定义了一个简单的异常类型，包含一个枚举类型来表示不同的错误类型，本类仅提供简单的错误信息，适用于catch块掌握详细信息的情况。
     */
    class exception
    {
    public:
        enum class type
        {
            // 文件操作
            /* 注：其中，fileErro是笼统的错误类型
              仅当前几种中的多种可能同时发生且不确定时才适宜使用*/
            FileNotFound,   // 文件未找到
            FileOpenError,  // 文件打开失败
            FileReadError,  // 文件读取失败
            FileWriteError, // 文件写入失败
            FileCloseError, // 文件关闭失败
            FileCorrupted,  // 文件损坏
            FileError,      // 文件相关错误

            //  参数与状态错误
            InvalidInput,  // 输入参数无效
            InvalidFormat, // 输入数据格式错误
            InvalidID,     // 输入ID无效
            InvalidData,   // 数据无效（空数据、损坏数据等）
            InvalidState,  // 当前状态不允许执行该操作（用户设置冲突）

            // 音频播放与解码错误
            PlaybackError, // 音频播放失败
            DecodeError,   // 音频解码失败

            // 内存错误
            OutOfMemory, // 内存不足
            MemoryError, // 内存分配失败

            // 无法描述因素
            CustomizedError, // 用户自定义错误
            UnknownError,    // 未知错误
        };

    private:
        type type_;

    public:
        exception() = delete;
        exception(type t) : type_(t) {}
        type getType() const { return type_; }
        ~exception() = default;
    };

    /**
     * @brief 扩展的yumo库异常类，错误信息使用宽字符字符串（const wchar_t*）存储
     *
     * 这个类继承自`yumo::exception`类，增加了一个成员变量`message_`来存储错误信息。构造函数接受一个异常类型和一个错误信息字符串，并提供了一个what()方法来获取错误信息。
     * @note wchar_t指针适宜指向静态字符串字面量，或者是由调用者管理生命周期的字符串，使用时需要注意确保指针指向的字符串在异常对象生命周期内有效。
     */
    class exception_ex : public exception
    {
    private:
        const wchar_t *message_;

    public:
        exception_ex() = delete;
        exception_ex(exception::type t, const wchar_t *msg) noexcept
            : exception(t), message_(msg) {}
        ~exception_ex() = default;
        const wchar_t *what() const noexcept { return message_; }
    };

    /**
     * @brief 扩展的yumo库异常类（第二类），错误信息使用标准库std::wstring存储
     *
     * 这个类继承自`yumo::exception`类，增加了一个成员变量`message_`来存储错误信息。构造函数接受一个异常类型和一个错误信息字符串，并提供了一个what()方法来获取错误信息。
     * @note 这个类适用于需要在catch块中获取详细错误信息的情况，可以通过what()方法获取到具体的错误描述，便于调试和用户提示。
     */
    class exception_ex2 : public exception
    {
    private:
        std::wstring message_;

    public:
        exception_ex2() = delete;
        exception_ex2(exception::type t, const std::wstring &msg) : exception(t), message_(msg) {}
        const std::wstring &what() const noexcept { return message_; }
        ~exception_ex2() = default;
    };

    /**
     * @brief 带有错误信息的异常类，错误信息使用宽字符字符串（const wchar_t*）存储
     *
     * 这个类不继承自任何一个现存异常类，是一个使用宽字符的std::exception简单实现，提供了一个what()方法来获取错误信息。
     * @note wchar_t指针适宜指向静态字符串字面量，或者是由调用者管理生命周期的字符串，使用时需要注意确保指针指向的字符串在异常对象生命周期内有效。
     */
    class w_exception
    {
    private:
        const wchar_t *message_;

    public:
        w_exception() = delete;
        w_exception(const wchar_t *msg) noexcept
            : message_(msg) {}
        ~w_exception() = default;
        const wchar_t *what() const noexcept { return message_; }
    };
}
#endif // YUMO_EXCEPT_HPP