#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <atomic>
#include <thread>
#include <chrono>

int main()
{
    #ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    #endif
    
    std::wcout << L"=== Yumo Audio 播放控制测试 ===" << std::endl;
    std::wcout << L"测试：预加载 -> 播放 -> 静音 -> 恢复 -> 挂起 -> 恢复" << std::endl;
    
    // 注意：请将测试音频替换为较长的音频文件（建议30秒以上）
    // 以便完整测试静音/挂起/恢复功能
    // 由于版权问题，测试音频文件未包含在项目中
    std::wcout << L"\n[提示] 开发者请替换测试音频为长音频文件以获得更好的测试效果" << std::endl;
    
    try {
        yumo::AudioPool& pool = yumo::AudioPool::getInstance();
        
        // 测试音频路径（请替换为实际的长音频文件）
        const wchar_t* files[] = {
            L"..\\audio\\test.wav",
            L"..\\audio\\test2.wav",
            L"..\\audio\\test3.wav"
        };
        
        // 预加载三段音频
        std::wcout << L"\n=== 预加载音频 ===" << std::endl;
        std::atomic<bool> ready1(false), ready2(false), ready3(false);
        size_t id1 = pool.preloadAudio(files[0], &ready1);
        size_t id2 = pool.preloadAudio(files[1], &ready2);
        size_t id3 = pool.preloadAudio(files[2], &ready3);
        
        std::wcout << L"预加载ID: " << id1 << L", " << id2 << L", " << id3 << std::endl;
        
        // 等待所有音频加载完成
        std::wcout << L"等待加载完成..." << std::endl;
        while (!ready1.load() || !ready2.load() || !ready3.load()) {
            Sleep(10);
        }
        std::wcout << L"所有音频加载完成！" << std::endl;
        
        // 同时播放三段音频
        std::wcout << L"\n=== 开始播放 ===" << std::endl;
        size_t inst1 = pool.addAudio(id1);
        size_t inst2 = pool.addAudio(id2);
        size_t inst3 = pool.addAudio(id3);
        pool.setGlobalMute(false); // 取消静音开始播放
        
        std::wcout << L"播放实例ID: " << inst1 << L", " << inst2 << L", " << inst3 << std::endl;
        std::wcout << L"播放实例数: " << pool.getPlayingCount() << std::endl;
        
        // 播放5秒
        std::wcout << L"\n[播放中] 等待5秒..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 静音5秒（position继续推进）
        std::wcout << L"\n=== 静音 ===" << std::endl;
        pool.setGlobalMute(true);
        std::wcout << L"[静音中] 等待5秒...（音频仍在播放，只是听不到）" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 恢复播放
        std::wcout << L"\n=== 恢复播放 ===" << std::endl;
        pool.setGlobalMute(false);
        std::wcout << L"[播放中] 等待5秒...（从静音结束的位置继续）" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 挂起5秒（position不推进）
        std::wcout << L"\n=== 挂起（停止） ===" << std::endl;
        pool.stopAll();
        std::wcout << L"[挂起中] 等待5秒...（音频暂停，position不变）" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 恢复播放
        std::wcout << L"\n=== 恢复播放 ===" << std::endl;
        pool.resume();
        std::wcout << L"[播放中] 等待5秒...（从挂起时的位置继续）" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 停止所有播放
        std::wcout << L"\n=== 测试结束 ===" << std::endl;
        pool.stopAll();
        std::wcout << L"已停止所有播放" << std::endl;
        
    } catch (const yumo::exception_ex& e) {
        std::wcout << L"发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::wcout << L"\n=== 测试完成 ===" << std::endl;
    return 0;
}
