# CuttyTerm 앱 아이콘(src/Resources/app.ico) 생성 스크립트.
# 의미: 탭이 여러 개 달린 터미널 창(여러 연결) + ">_" 프롬프트.
# 크기별로 따로 그려(작은 크기는 선을 굵게) 16px 에서도 형태가 뭉개지지 않게 한다.
# 사용법: python tools/make_app_icon.py [출력경로]
import sys
from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SUPERSAMPLE = 8


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def draw_icon(size):
    s = size * SUPERSAMPLE
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))

    # 1) 바탕: 둥근 사각형 + 위->아래 파란 그라데이션
    gradient = Image.new('RGBA', (s, s))
    gd = ImageDraw.Draw(gradient)
    top, bottom = (56, 140, 245, 255), (18, 70, 170, 255)
    for y in range(s):
        gd.line([(0, y), (s, y)], fill=lerp(top, bottom, y / (s - 1)))
    mask = Image.new('L', (s, s), 0)
    margin = s * 0.04
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, s - margin, s - margin], radius=s * 0.22, fill=255)
    img.paste(gradient, (0, 0), mask)

    d = ImageDraw.Draw(img)
    small = size <= 24  # 아주 작은 크기는 탭을 하나로 단순화하고 선을 굵게

    # 2) 터미널 창: 창 폭 전체를 덮는 탭 바(브라우저처럼) + 그 안의 탭 2개.
    #    탭 바가 창 전체 폭이라 폴더 아이콘처럼 보이지 않는다.
    win_l, win_r = s * 0.14, s * 0.86
    win_t, win_b = s * 0.17, s * 0.84
    bar_h = s * (0.13 if small else 0.19)
    screen_t = win_t + bar_h
    screen = (14, 22, 33, 255)
    bar = (198, 214, 236, 255)
    inactive = (150, 170, 198, 255)
    radius = s * 0.08
    d.rounded_rectangle([win_l, win_t, win_r, win_b], radius=radius, fill=bar)
    d.rounded_rectangle([win_l, screen_t - radius, win_r, win_b], radius=radius, fill=screen)
    d.rectangle([win_l, screen_t - radius, win_r, screen_t], fill=screen)  # 화면 위쪽은 각지게
    if not small:
        tab_t = win_t + bar_h * 0.28
        tab_r = s * 0.035
        # 활성 탭: 화면과 같은 색으로 아래 화면과 이어짐 / 비활성 탭: 회색
        d.rounded_rectangle([win_l + s * 0.05, tab_t, s * 0.47, screen_t + tab_r], radius=tab_r, fill=screen)
        d.rounded_rectangle([s * 0.49, tab_t, s * 0.70, screen_t - s * 0.012], radius=tab_r, fill=inactive)
    win_t = screen_t

    # 3) ">" 프롬프트와 "_" 커서
    stroke = s * (0.085 if small else 0.06)
    green = (74, 222, 128, 255)
    cx, cy = s * 0.27, (win_t + win_b) / 2
    arm = s * (0.13 if small else 0.11)
    d.line([(cx, cy - arm), (cx + arm, cy), (cx, cy + arm)], fill=green, width=int(stroke), joint='curve')
    for px, py in [(cx, cy - arm), (cx + arm, cy), (cx, cy + arm)]:  # 둥근 끝
        r = stroke / 2
        d.ellipse([px - r, py - r, px + r, py + r], fill=green)
    cur_l, cur_r = cx + arm + s * 0.07, s * (0.70 if small else 0.68)
    cur_y = cy + arm - stroke / 2
    d.rounded_rectangle([cur_l, cur_y, cur_r, cur_y + stroke], radius=stroke / 2, fill=(235, 245, 255, 255))

    return img.resize((size, size), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'src/Resources/app.ico'
    images = [draw_icon(size) for size in SIZES]
    images[-1].save(out, format='ICO', sizes=[(i.width, i.height) for i in images], append_images=images[:-1])
    print('wrote', out)


if __name__ == '__main__':
    main()
