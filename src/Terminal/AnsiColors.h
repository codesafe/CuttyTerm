#pragma once
#include <cstdint>

namespace Cutty
{
    // xterm 계열 표준 16색 팔레트 (0xRRGGBB).
    inline uint32_t GetAnsi16Color(int index)
    {
        static const uint32_t palette[16] =
        {
            0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, // 검정,빨강,초록,노랑
            0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5, // 파랑,보라,청록,흰색
            0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, // 밝은 계열
            0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF,
        };
        return palette[index & 0xF];
    }

    // 256색 인덱스(0-255)를 0xRRGGBB 로 변환. 0-15: 기본 팔레트, 16-231: 6x6x6 컬러큐브, 232-255: 그레이스케일.
    inline uint32_t GetAnsi256Color(int index)
    {
        if (index < 16) return GetAnsi16Color(index);

        if (index < 232)
        {
            int i = index - 16;
            int r = i / 36;
            int g = (i / 6) % 6;
            int b = i % 6;
            auto scale = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
            return (static_cast<uint32_t>(scale(r)) << 16) | (static_cast<uint32_t>(scale(g)) << 8) | scale(b);
        }

        int gray = 8 + (index - 232) * 10;
        return (static_cast<uint32_t>(gray) << 16) | (static_cast<uint32_t>(gray) << 8) | gray;
    }
}
