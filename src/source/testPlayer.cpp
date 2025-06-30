#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>   //std::unique_ptr

#include "../include/MediaPlayer.h"

using namespace std;

int main(int argc, char* argv[]) {
    //检查命令行参数
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <video_file>" << endl;
        return 1;
    }

    // 在应用程序的最开始初始化SDL，确保整个程序中SDL_Init只被有效调用一次
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        cerr << "FATAL: SDL could not initialize! SDL_Error: " << SDL_GetError() << endl;
        return 1;
    }
    cout << "Application: SDL system initialized." << endl;

    // 输入文件路径；从命令行参数获取
    const string filepath = argv[1];
    std::unique_ptr<MediaPlayer> player;

    cout << "TestPlayer: Attempting to create MediaPlayer for file:" << filepath << endl;

    try {
        player = std::make_unique<MediaPlayer>(filepath);
        cout << "TestPlayer: MediaPlayer created. Starting main loop." << endl;

        if (player->runMainLoop()!=0) {
            cerr << "testPlayer: MediaPlayer runMainLoop exited with an error." << endl;
        }
    }
    catch (const std::runtime_error& e) {
        cerr << "TestPlayer: Runtime error during MediaPlayer lifecycle: " << e.what() << endl;
        // player 会自动销毁，触发析构和清理
        // 如果构造函数抛出，player 可能为空。
    }
   
    // player 离开作用域，其析构函数被调用，所有播放器资源被释放
    player.reset();
    cout << "Application: MediaPlayer object destroyed." << endl;

    // 在应用程序的最后退出SDL
    SDL_Quit();
    cout << "Application: SDL system quit. Exiting." << endl;

    return 0;
}