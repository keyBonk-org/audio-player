#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
#endif

    std::wcout << L"=== Yumo Audio 简单测试 ===" << std::endl;

    // 打印当前工作目录
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wcout << L"当前工作目录: " << cwd << std::endl;

    try
    {
        // 使用相对路径测试
        const wchar_t *filename = L"..\\audio\\test.wav";
        std::wcout << L"\n添加音频: " << filename << std::endl;

        yumo::readySign ready(false);
        size_t preloadedId = yumo::preloadAudio(filename, &ready);
        std::wcout << L"预加载ID: " << preloadedId << std::endl;

        // 等待加载完成
        std::wcout << L"等待加载..." << std::endl;
        while (!ready)
        {
            Sleep(10);
        }
        std::wcout << L"加载完成！" << std::endl;

        // 添加播放
        std::wcout << L"添加播放..." << std::endl;
        size_t instanceId = yumo::addAudio(preloadedId);
        std::wcout << L"播放实例ID: " << instanceId << std::endl;
        std::wcout << L"播放实例数: " << yumo::getPlayingCount() << std::endl;

        // 等待播放
        std::wcout << L"\n播放中...按 Enter 键停止" << std::endl;
        std::wcin.get();

        yumo::global.stop = true;
        std::wcout << L"已停止播放" << std::endl;
    }
    catch (const yumo::exception_ex &e)
    {
        std::wcout << L"发生异常: " << e.what() << std::endl;
        return 1;
    }

    std::wcout << L"\n=== 测试完成 ===" << std::endl;
    return 0;
}
