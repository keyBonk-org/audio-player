#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <atomic>

int main()
{
    #ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    #endif
    
    std::wcout << L"=== Yumo Audio addAudio 字符串版本测试 ===" << std::endl;
    
    // 打印当前工作目录
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wcout << L"当前工作目录: " << cwd << std::endl;
    
    try {
        // 获取音频池单例
        yumo::AudioPool& pool = yumo::AudioPool::getInstance();
        
        // 使用绝对路径测试
        const wchar_t* files[] = {
            L"..\\audio\\test.wav",
            L"..\\audio\\test2.wav", 
            L"..\\audio\\test3.wav"
        };
        
        std::wcout << L"\n使用 addAudio(filename) 字符串版本（便利接口，异步加载播放）..." << std::endl;
        size_t instanceIds[3] = {0, 0, 0};
        std::atomic<bool> readyFlags[3] = {false, false, false};

        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
            std::wcout << L"\n添加音频: " << files[i] << std::endl;
            // 便利接口，异步加载并播放，播放完成后自动清理预加载对象
            pool.addAudio(files[i], 1.0f, &instanceIds[i], &readyFlags[i]);
            std::wcout << L"  -> 后台加载播放，instanceId 将在播放开始后写入" << std::endl;
        }

        std::wcout << L"\n等待所有音频加载完成..." << std::endl;
        for (size_t i = 0; i < 3; ++i) {
            while (!readyFlags[i].load()) {
                Sleep(10);
            }
            std::wcout << L"  音频 " << i << L" 加载完成，instanceId=" << instanceIds[i] << std::endl;
        }
        
        std::wcout << L"\n所有音频已提交加载！" << std::endl;
        std::wcout << L"预加载音频数: " << pool.getPreloadedCount() << std::endl;
        
        // 等待用户输入停止
        std::wcout << L"\n按 Enter 键停止" << std::endl;
        std::wcin.get();
        
        pool.stopAll();
        std::wcout << L"已停止所有播放" << std::endl;
        
    } catch (const yumo::exception_ex& e) {
        std::wcout << L"发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::wcout << L"\n=== 测试完成 ===" << std::endl;
    return 0;
}
