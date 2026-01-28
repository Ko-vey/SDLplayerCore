/*
 * SDLplayerCore - An audio and video player.
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

#pragma once

#include "SDL2/SDL.h"
#include "SDL2/SDL_ttf.h"
#include "PlayerDebugStats.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

class OSDLayer {
private:
    TTF_Font* m_font = nullptr;
    bool m_visible = true;
    const int FONT_SIZE = 16;
    const int LINE_HEIGHT = 20;

private:
    // 将时钟类型转换为字符串
    std::string getClockSourceName(int type) const {
        switch (type) {
        case -1: return "Unknown (Syncing...)";
        case 0:  return "Audio Master"; // MasterClockType::AUDIO
        case 1:  return "External (System)"; // MasterClockType::EXTERNAL
        default: return "Invalid";
        }
    }
public:
    OSDLayer() = default;
    ~OSDLayer() { 
        cleanup();
    }

    bool init(const std::string& fontPath) {
        if (TTF_Init() == -1) {
            printf("TTF_Init: %s\n", TTF_GetError());
            return false;
        }
        m_font = TTF_OpenFont(fontPath.c_str(), FONT_SIZE);
        if (!m_font) {
            printf("TTF_OpenFont: %s\n", TTF_GetError());
            // 可以在这设置一个备用路径，或者硬编码默认行为
            return false;
        }
        return true;
    }

    void cleanup() {
        if (m_font) {
            TTF_CloseFont(m_font);
            m_font = nullptr;
        }
        TTF_Quit();
    }

    void toggle() { 
        m_visible = !m_visible;
    }

    void render(SDL_Renderer* renderer, const PlayerDebugStats& stats, int windowW, int windowH) {
        if (!m_visible || !m_font || !renderer) return;

        std::vector<std::string> lines;
        std::ostringstream oss;

        // --- Player State ---
        oss << "State: ";
        int stateVal = stats.current_state.load();
        switch (stateVal) {
        case 0: oss << "IDLE"; break;
        case 1: oss << "BUFFERING..."; break;
        case 2: oss << "PLAYING"; break;
        case 3: oss << "PAUSED"; break;
        case 4: oss << "STOPPED"; break;
        default: oss << "UNKNOWN (" << stateVal << ")"; break;
        }
        lines.push_back(oss.str());
        oss.str(""); oss.clear();

        // --- V-Q Info ---
        // duration 转换为秒，保留2位小数
        double vq_sec = stats.vq_duration_ms.load() / 1000.0;
        oss << "V-Q: " << stats.vq_size.load() << " pkts / "
            << std::fixed << std::setprecision(2) << vq_sec << " sec";
        lines.push_back(oss.str());
        oss.str(""); oss.clear();

        // --- A-V Sync ---
        // 预先加载原子变量
        int clockSrcType = stats.clock_source_type.load();
        double masterTime = stats.master_clock_val.load();
        double videoPts = stats.video_current_pts.load();
        double avDiff = stats.av_diff_ms.load();

        // Clock Status (时钟源与当前时间) 
        oss << "Clock: " << getClockSourceName(clockSrcType);
        // 只有在时钟已同步(非-1)时，且取到的时间确实是有效数字时才显示主时钟时间
        if (clockSrcType != -1 && !std::isnan(masterTime)) {
            oss << " | T: " << std::fixed << std::setprecision(2) << masterTime << "s";
        }
        lines.push_back(oss.str());
        oss.str(""); oss.clear();

        // A-V Sync (同步差值与视频PTS)
        // 如果处于 Syncing 状态，Diff 数据可能巨大或为 0，隐藏并显示占位符
        if (clockSrcType == -1) {
            oss << "Sync: --";
        }
        else {
            oss << "Sync: " << std::fixed << std::setprecision(1) << avDiff << " ms"
                << " (V-PTS: " << std::setprecision(2) << videoPts << ")";
        }
        lines.push_back(oss.str());
        oss.str(""); oss.clear();

        // --- FPS ---
        oss << "FPS: Decode " << stats.decode_fps.getFPS()
            << " / Render " << stats.render_fps.getFPS();
        lines.push_back(oss.str());

        // --- 开始绘制 ---

        // 背景框配置
        int padding = 10;
        int boxW = 350; // 根据实际情况调整
        int boxH = static_cast<int>(lines.size() * LINE_HEIGHT) + padding * 2;
        int startX = 10;
        int startY = 10;

        // 绘制半透明背景
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128); // 黑色，50%透明
        SDL_Rect bgRect = { startX, startY, boxW, boxH };
        SDL_RenderFillRect(renderer, &bgRect);

        // 绘制文字
        SDL_Color textColor = { 255, 255, 255, 255 }; // 白色
        int currentY = startY + padding;

        for (const auto& line : lines) {
            SDL_Surface* surface = TTF_RenderText_Blended(m_font, line.c_str(), textColor);
            if (surface) {
                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                if (texture) {
                    SDL_Rect destRect = { startX + padding, currentY, surface->w, surface->h };
                    SDL_RenderCopy(renderer, texture, nullptr, &destRect);
                    SDL_DestroyTexture(texture);
                }
                SDL_FreeSurface(surface);
            }
            currentY += LINE_HEIGHT;
        }
    }
};