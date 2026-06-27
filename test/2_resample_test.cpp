#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <atomic>

void testAudioFile(const wchar_t* filename)
{
    try
    {
        std::wcout << L"\n=== 测试文件: " << filename << L" ===" << std::endl;
        
        // 获取音频池单例
        yumo::AudioPool& pool = yumo::AudioPool::getInstance();
        
        // 预加载音频
        std::wcout << L"正在预加载音频..." << std::endl;
        std::atomic<bool> ready(false);
        size_t preloadedId = pool.preloadAudio(filename, &ready);
        
        // 等待加载完成
        while (!ready.load()) {
            Sleep(10);
        }
        std::wcout << L"预加载完成！ID: " << preloadedId << std::endl;
        
        // 添加到播放队列
        std::wcout << L"\n按 Enter 键播放音频...";
        std::wcin.get();

        pool.resume();  // 重置停止状态，确保新音频能正常播放
     
        size_t instanceId = pool.addAudio(preloadedId);
        pool.setGlobalMute(false); // 取消静音开始播放
   
        std::wcout << L"正在播放... 播放实例ID: " << instanceId << std::endl;
        
        // 等待播放完成
        std::wcout << L"按 Enter 键停止...";
        std::wcin.get();
        
        pool.stopAll();
        std::wcout << L"播放已停止！" << std::endl;
        
    }
    catch (const yumo::exception_ex& e)
    {
        std::wcout << L"发生异常: " << e.what() << std::endl;
    }
}

int main()
{
    #ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    #endif
    
    std::wcout << L"=== Yumo Audio 重采样测试程序 ===" << std::endl;
    
    // 测试三个音频文件
    const wchar_t* files[] = {
        L"c:\\小狄\\audio-player\\test\\audio\\test.wav",
        L"c:\\小狄\\audio-player\\test\\audio\\test2.wav", 
        L"c:\\小狄\\audio-player\\test\\audio\\test3.wav"
    };
    
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
    {
        testAudioFile(files[i]);
    }
    
    std::wcout << L"\n=== 测试完成 ===" << std::endl;
    
    return 0;
}