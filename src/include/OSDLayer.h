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
        oss << "A-V Diff: " << std::fixed << std::setprecision(1) << stats.av_diff_ms.load() << " ms "
            << "(VPts:" << std::setprecision(2) << stats.video_current_pts.load()
            << " - Clock:" << stats.master_clock_val.load() << ")";
        lines.push_back(oss.str());
        oss.str(""); oss.clear();

        // --- Clock Source ---
        int src = stats.clock_source_type.load();
        std::string srcStr = (src == 0) ? "Audio" : (src == 1 ? "External" : "Video");
        oss << "Clock Source: " << srcStr;
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