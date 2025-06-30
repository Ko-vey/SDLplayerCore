#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>   //std::unique_ptr

#include "../include/MediaPlayer.h"

using namespace std;

int main(int argc, char* argv[]) {
    //��������в���
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <video_file>" << endl;
        return 1;
    }

    // ��Ӧ�ó�����ʼ��ʼ��SDL��ȷ������������SDL_Initֻ����Ч����һ��
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        cerr << "FATAL: SDL could not initialize! SDL_Error: " << SDL_GetError() << endl;
        return 1;
    }
    cout << "Application: SDL system initialized." << endl;

    // �����ļ�·�����������в�����ȡ
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
        // player ���Զ����٣���������������
        // ������캯���׳���player ����Ϊ�ա�
    }
   
    // player �뿪���������������������ã����в�������Դ���ͷ�
    player.reset();
    cout << "Application: MediaPlayer object destroyed." << endl;

    // ��Ӧ�ó��������˳�SDL
    SDL_Quit();
    cout << "Application: SDL system quit. Exiting." << endl;

    return 0;
}