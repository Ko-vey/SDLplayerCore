/*
 * SDLplayerCore - An audio/video player core.
 * Copyright (C) 2025 Kovey <zzwaaa0396@qq.com>
 *
 * This file is part of SDLplayerCore.
 *
 * SDLplayerCore is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <string>
#include <stdexcept>
#include <memory>
#include <vector>
#include <limits> // std::numeric_limits

#include "../include/MediaPlayer.h"

/**
* @brief 在程序退出前暂停，等待用户输入，防止控制台窗口闪退
*/
void pause_before_exit() {
    std::cout << "\nPress Enter to exit..." << std::endl;
    // 清空输入缓冲区，并等待用户按回车
    std::cin.clear();   // 重置输入流状态
    // 清空输入缓冲区
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();     // 等待用户输入
}

/**
 * @brief 从字符串中移除所有指定的引号字符.
 *
 * 该函数会移除路径中包含的所有半角和全角引号.
 * 例如: "C:/'My Videos'/test.mp4" -> C:/My Videos/test.mp4
 *
 * @param path 引用传递的字符串，函数将直接修改此字符串.
 */
void remove_all_quotes(std::string& path) {
    // 定义一个包含所有需要移除的引号的列表
    // 包含： " ' “ ” ‘ ’
    const std::vector<std::string> quotes_to_remove = {
        "\"", "'", "“", "”", "‘", "’"
    };

    for (const auto& quote : quotes_to_remove) {
        size_t pos = 0;
        // 在字符串中循环查找并删除当前类型的引号
        while ((pos = path.find(quote, pos)) != std::string::npos) {
            // 从找到的位置删除引号（长度为 quote.length()）
            path.erase(pos, quote.length()); // erase执行后pos指向被删除引号后面的字符
        }
    }
}

int main(int argc, char* argv[]) {
    std::string filepath;

    // 1. 判断输入来源：命令行参数或用户交互式输入
    if (argc >= 2) {
        // 模式一：从命令行参数获取文件路径
        filepath = argv[1];
    }
    else {
        // 模式二：提示用户在控制台输入文件路径
        std::cout << "Please enter the path of media file and press Enter:" << std::endl;
        // 使用 getline 读取可能包含空格的完整路径
        std::getline(std::cin, filepath);

        if (filepath.empty()) {
            std::cerr << "Error: No file path was provided." << std::endl;
            pause_before_exit();
            return 1;
        }
    }

    // 2. 清理路径中的所有引号
    remove_all_quotes(filepath);

    // (可选) 在这里打印清理后的路径用于调试
    std::cout << "Cleaned file path: " << filepath << std::endl;

    // 3. 初始化SDL库
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "FATAL: Could not initialize SDL. SDL_Error: " << SDL_GetError() << std::endl;
        pause_before_exit();
        return 1;
    }

    // 4. 主逻辑：创建并运行播放器
    try {
        auto player = std::make_unique<MediaPlayer>(filepath);

        if (player->runMainLoop() != 0) {
            std::cerr << "Error: MediaPlayer main loop exited unexpectedly." << std::endl;
            SDL_Quit();
            pause_before_exit();
            return 1;
        }
    }
    catch (const std::runtime_error& e) {
        std::cerr << "Runtime Error: " << e.what() << std::endl;
        SDL_Quit();
        pause_before_exit();
        return 1;
    }

    // 5. 成功播放完成后，清理并退出SDL
    SDL_Quit();

    return 0;
}
