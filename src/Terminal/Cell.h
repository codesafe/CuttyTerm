#pragma once
#include <cstdint>

namespace Cutty
{
    // 하나의 화면 셀에 표시되는 정보 (문자, 색상, 속성).
    // codepoint 는 UTF-32 값이며 CJK 등 2칸을 차지하는 문자는 isWide=true 로 표시하고
    // 다음 셀은 isTrailing=true 인 빈 셀로 채운다.
    struct Cell
    {
        char32_t codepoint = U' ';
        uint32_t foreground = 0x00D4D4D4; // 0x00RRGGBB (기본값: 밝은 회색)
        uint32_t background = 0x001E1E1E; // 기본 배경(어두운 회색)
        bool bold = false;
        bool underline = false;
        bool reverse = false;
        bool dim = false;           // SGR 2: 흐리게
        bool italic = false;        // SGR 3: 기울임 (렌더러가 기울여 그림)
        bool strikethrough = false; // SGR 9: 취소선
        bool hidden = false;        // SGR 8: 숨김
        bool isWide = false;     // 이 셀이 2칸짜리 문자의 시작인가
        bool isTrailing = false; // 이 셀이 2칸짜리 문자의 두번째 칸(빈 칸)인가

        bool operator==(const Cell& other) const = default;
    };

    // SGR(Select Graphic Rendition) 처리 중 유지되는 "현재 그리기 상태".
    struct GraphicState
    {
        uint32_t foreground = 0x00D4D4D4;
        uint32_t background = 0x001E1E1E;
        bool bold = false;
        bool underline = false;
        bool reverse = false;
        bool dim = false;
        bool italic = false;
        bool strikethrough = false;
        bool hidden = false;
    };
}
