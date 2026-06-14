#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <iostream>
#include <io.h>
#include <fcntl.h>

int main()
{
    #ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    #endif
    
    std::wcout << L"=== Yumo Audio 音频池混合测试 ===" << std::endl;
    
    try {
        // 获取音频池单例
        yumo::AudioPool& pool = yumo::AudioPool::getInstance();
        
        // 添加音频文件到音频池
        const wchar_t* files[] = {
            L"../audio/test.wav",
            L"../audio/test2.wav", 
            L"../audio/test3.wav"
        };
        
        std::wcout << L"\n正在加载音频文件..." << std::endl;
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
            std::wcout << L"添加音频: " << files[i] << std::endl;
            size_t audioId = pool.addAudio(files[i]);
            std::wcout << L"  -> 音频ID: " << audioId << std::endl;
            
            // 设置不同音量（测试混合效果）
            float volume = 0.5f + (i * 0.2f);
            if (volume > 1.0f) volume = 1.0f;
            pool.setVolume(audioId, volume);
            std::wcout << L"  -> 音量设置: " << volume << std::endl;
        }
        
        std::wcout << L"\n音频池中共计 " << pool.getAudioCount() << L" 个音频" << std::endl;
        
        // 开始混合播放
        std::wcout << L"\n按 Enter 键开始混合播放..." << std::endl;
        std::wcin.get();
        
        std::wcout << L"正在混合播放所有音频..." << std::endl;
        pool.playAll();
        
        // 等待播放结束（播放是异步的，这里简单等待）
        std::wcout << L"播放中...按 Enter 键退出" << std::endl;
        std::wcin.get();
        
        pool.stop();
        
    } catch (const yumo::exception_ex& e) {
        std::wcout << L"发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::wcout << L"\n=== 测试完成 ===" << std::endl;
    return 0;
}