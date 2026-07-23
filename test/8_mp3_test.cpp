#include "../src/audioPlayer.hpp"
#include "../src/yumo_except.hpp"
#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <string>

struct AudioStatus
{
    size_t preloadedId;
    size_t instanceId;
    bool isPlaying;
    float volume;
    bool isMuted;
    bool isPaused;
};

void printStatus(AudioStatus audios[], size_t count)
{
    std::wcout << L"\n当前音频状态:" << std::endl;
    std::wcout << L"  全局状态: 音量" << yumo::global.volume
               << L"  " << (yumo::global.mute ? L"全局静音" : L"全局非静音")
               << L"  " << (yumo::global.stop ? L"全局停止" : L"全局运行") << std::endl;
    for (size_t i = 0; i < count; ++i)
    {
        std::wcout << L"  " << (i + 1) << L"：";
        if (audios[i].isPlaying)
        {
            std::wcout << L"已开始播放";
            std::wcout << L"  音量" << audios[i].volume;
            std::wcout << L"  " << (audios[i].isMuted ? L"已静音" : L"未静音");
            std::wcout << L"  " << (audios[i].isPaused ? L"已暂停" : L"未暂停");
        }
        else
        {
            std::wcout << L"未开始播放";
        }
        std::wcout << std::endl;
    }
}

void printMenu()
{
    std::wcout << L"\n操作菜单:" << std::endl;
    std::wcout << L"  1. 播放音频" << std::endl;
    std::wcout << L"  2. 调节音量" << std::endl;
    std::wcout << L"  3. 暂停音频" << std::endl;
    std::wcout << L"  4. 恢复音频" << std::endl;
    std::wcout << L"  5. 静音/取消静音" << std::endl;
    std::wcout << L"  6. 停止所有播放" << std::endl;
    std::wcout << L"  7. 恢复全部播放" << std::endl;
    std::wcout << L"  8. 设置全局音量" << std::endl;
    std::wcout << L"  9. 全局静音/取消静音" << std::endl;
    std::wcout << L"  10. 退出" << std::endl;
    std::wcout << L"请输入选择: ";
}

bool readInt(int &out, const wchar_t *prompt)
{
    std::wcout << prompt;
    if (!(std::wcin >> out))
    {
        std::wcin.clear();
        std::wcin.ignore(1024, L'\n');
        std::wcout << L"输入无效，请输入整数" << std::endl;
        return false;
    }
    return true;
}

bool readFloat(float &out, const wchar_t *prompt)
{
    std::wcout << prompt;
    if (!(std::wcin >> out))
    {
        std::wcin.clear();
        std::wcin.ignore(1024, L'\n');
        std::wcout << L"输入无效，请输入数字" << std::endl;
        return false;
    }
    return true;
}

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
#endif

    std::wcout << L"=== Yumo Audio 精细控制测试 ===" << std::endl;

    try
    {
        const wchar_t *files[] = {
            L"..\\audio\\test1.mp3",
            L"..\\audio\\test2.mp3",
            L"..\\audio\\test3.mp3"};

        AudioStatus audios[3] = {};
        yumo::readySign readyFlags[3] = {false, false, false};

        std::wcout << L"\n预加载音频..." << std::endl;
        for (size_t i = 0; i < 3; ++i)
        {
            audios[i].preloadedId = yumo::preloadAudio(files[i], &readyFlags[i]);
            audios[i].isPlaying = false;
            audios[i].volume = 1.0f;
            audios[i].isMuted = false;
            audios[i].isPaused = false;
            std::wcout << L"  预加载 " << files[i] << L"，preloadedId=" << audios[i].preloadedId << std::endl;
        }

        std::wcout << L"\n等待预加载完成..." << std::endl;
        for (size_t i = 0; i < 3; ++i)
        {
            while (!readyFlags[i])
            {
                Sleep(10);
            }
            std::wcout << L"  音频 " << (i + 1) << L" 预加载完成" << std::endl;
        }

        int choice;
        do
        {
            printStatus(audios, 3);
            printMenu();
            if (!readInt(choice, L""))
            {
                continue;
            }

            switch (choice)
            {
            case 1:
            {
                int idx;
                if (!readInt(idx, L"请输入音频编号(1-3): "))
                {
                    break;
                }
                if (idx < 1 || idx > 3)
                {
                    std::wcout << L"无效编号" << std::endl;
                    break;
                }
                if (audios[idx - 1].isPlaying)
                {
                    std::wcout << L"该音频已在播放中" << std::endl;
                    break;
                }
                audios[idx - 1].instanceId = yumo::addAudio(audios[idx - 1].preloadedId, audios[idx - 1].volume);
                audios[idx - 1].isPlaying = true;
                std::wcout << L"开始播放音频 " << idx << L"，instanceId=" << audios[idx - 1].instanceId << std::endl;
                break;
            }
            case 2:
            {
                int idx;
                if (!readInt(idx, L"请输入音频编号(1-3): "))
                {
                    break;
                }
                if (idx < 1 || idx > 3)
                {
                    std::wcout << L"无效编号" << std::endl;
                    break;
                }
                if (!audios[idx - 1].isPlaying)
                {
                    std::wcout << L"该音频未播放" << std::endl;
                    break;
                }
                float vol;
                if (!readFloat(vol, L"请输入音量(0.0-1.0): "))
                {
                    break;
                }
                if (vol < 0.0f || vol > 1.0f)
                {
                    std::wcout << L"音量必须在 0.0-1.0 之间" << std::endl;
                    break;
                }
                yumo::setVolume(audios[idx - 1].instanceId, vol);
                audios[idx - 1].volume = vol;
                std::wcout << L"音量已设置为 " << vol << std::endl;
                break;
            }
            case 3:
            {
                int idx;
                if (!readInt(idx, L"请输入音频编号(1-3): "))
                {
                    break;
                }
                if (idx < 1 || idx > 3)
                {
                    std::wcout << L"无效编号" << std::endl;
                    break;
                }
                if (!audios[idx - 1].isPlaying)
                {
                    std::wcout << L"该音频未播放" << std::endl;
                    break;
                }
                if (audios[idx - 1].isPaused)
                {
                    std::wcout << L"该音频已暂停" << std::endl;
                    break;
                }
                yumo::stop(audios[idx - 1].instanceId);
                audios[idx - 1].isPaused = true;
                std::wcout << L"已暂停音频 " << idx << std::endl;
                break;
            }
            case 4:
            {
                int idx;
                if (!readInt(idx, L"请输入音频编号(1-3): "))
                {
                    break;
                }
                if (idx < 1 || idx > 3)
                {
                    std::wcout << L"无效编号" << std::endl;
                    break;
                }
                if (!audios[idx - 1].isPlaying)
                {
                    std::wcout << L"该音频未播放" << std::endl;
                    break;
                }
                if (!audios[idx - 1].isPaused)
                {
                    std::wcout << L"该音频未暂停" << std::endl;
                    break;
                }
                yumo::resume(audios[idx - 1].instanceId);
                audios[idx - 1].isPaused = false;
                std::wcout << L"已恢复音频 " << idx << std::endl;
                break;
            }
            case 5:
            {
                int idx;
                if (!readInt(idx, L"请输入音频编号(1-3): "))
                {
                    break;
                }
                if (idx < 1 || idx > 3)
                {
                    std::wcout << L"无效编号" << std::endl;
                    break;
                }
                if (!audios[idx - 1].isPlaying)
                {
                    std::wcout << L"该音频未播放" << std::endl;
                    break;
                }
                bool newMuted = !audios[idx - 1].isMuted;
                yumo::setMuted(audios[idx - 1].instanceId, newMuted);
                audios[idx - 1].isMuted = newMuted;
                std::wcout << L"音频 " << idx << (newMuted ? L"已静音" : L"取消静音") << std::endl;
                break;
            }
            case 6:
            {
                yumo::global.stop = true;
                for (size_t i = 0; i < 3; ++i)
                {
                    audios[i].isPaused = true;
                }
                std::wcout << L"已停止所有播放" << std::endl;
                break;
            }
            case 7:
            {
                yumo::global.stop = false;
                for (size_t i = 0; i < 3; ++i)
                {
                    audios[i].isPaused = false;
                }
                std::wcout << L"已恢复全部播放" << std::endl;
                break;
            }
            case 8:
            {
                float vol;
                if (!readFloat(vol, L"请输入全局音量(0.0-1.0): "))
                {
                    break;
                }
                if (vol < 0.0f || vol > 1.0f)
                {
                    std::wcout << L"音量必须在 0.0-1.0 之间" << std::endl;
                    break;
                }
                yumo::global.volume = vol;
                std::wcout << L"全局音量已设置为 " << vol << std::endl;
                break;
            }
            case 9:
            {
                bool newMuted = !yumo::global.mute;
                yumo::global.mute = newMuted;
                std::wcout << (newMuted ? L"已全局静音" : L"已取消全局静音") << std::endl;
                break;
            }
            case 10:
            {
                std::wcout << L"退出程序" << std::endl;
                break;
            }
            default:
            {
                std::wcout << L"无效选择" << std::endl;
                break;
            }
            }
        } while (choice != 10);
    }
    catch (const yumo::exception_ex2 &e)
    {
        std::wcerr << L"错误: " << e.what() << std::endl;
        return 1;
    }
    catch (const yumo::exception_ex &e)
    {
        std::wcerr << L"错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}