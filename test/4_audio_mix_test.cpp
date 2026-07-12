#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <thread>
#include <chrono>

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
#endif

    std::wcout << L"=== Yumo Audio 混合播放测试 ===" << std::endl;

    try
    {
        // 预加载三个音频文件（异步）
        const wchar_t *files[] = {
            L"../audio/test.wav",
            L"../audio/test2.wav",
            L"../audio/test3.wav"};

        std::wcout << L"\n异步预加载音频文件..." << std::endl;
        size_t preloadedIds[3];
        yumo::readySign readyFlags[3] = {false, false, false};

        for (size_t i = 0; i < 3; ++i)
        {
            std::wcout << L"  预加载: " << files[i] << L" (后台加载)" << std::endl;
            preloadedIds[i] = yumo::preloadAudio(files[i], &readyFlags[i]);
            std::wcout << L"  -> 预加载ID: " << preloadedIds[i] << std::endl;
        }

        std::wcout << L"\n等待所有预加载完成..." << std::endl;
        for (size_t i = 0; i < 3; ++i)
        {
            while (!readyFlags[i])
            {
                Sleep(10);
            }
            std::wcout << L"  音频 " << i << L" 加载完成" << std::endl;
        }

        std::wcout << L"\n预加载完成，共 " << yumo::getPreloadedCount() << L" 个音频" << std::endl;
        std::wcout << L"每5秒添加一个音频进行播放..." << std::endl;

        // 每5秒添加一个音频，直到三个都开始播放
        for (size_t i = 0; i < 3; ++i)
        {
            std::wcout << L"\n[第 " << (i + 1) << L" 次] 等待5秒..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));

            std::wcout << L"[第 " << (i + 1) << L" 次] 添加预加载音频 ID=" << preloadedIds[i] << L" 到播放池" << std::endl;
            size_t instanceId = yumo::addAudio(preloadedIds[i]);
            std::wcout << L"  -> 播放实例ID: " << instanceId << std::endl;
            std::wcout << L"  当前播放实例数: " << yumo::getPlayingCount() << std::endl;
        }

        std::wcout << L"\n所有音频已开始播放！" << std::endl;
        std::wcout << L"按 Enter 键停止所有播放..." << std::endl;
        std::wcin.get();

        yumo::global.stop = true;
        std::wcout << L"已停止所有播放" << std::endl;
    }
    catch (const yumo::exception_ex &e)
    {
        std::wcout << L"发生异常: " << e.what() << std::endl;
        return 1;
    }

    std::wcout << L"\n=== 测试完成 ===" << std::endl;
    return 0;
}