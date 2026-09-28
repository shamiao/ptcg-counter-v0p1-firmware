#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen7segtab.py —— 生成 display.c 中的 HT1621 字形查表 g_7segtab[3][36][2]。

数据来源（改了任何一处都要重跑本脚本，再整块替换 display.c 里的表）：
  1. dev-notes/7seg-charset.html —— 字形定义：哪个字符点亮哪些段
  2. hardware-definition.h       —— COM/SEG 接线：段信号落在 HT1621 哪个 nibble 哪一位

用法：
  python tools/gen7segtab.py > tab.txt
  把 stdout 的 C 代码整块替换 display.c 中 g_7segtab 的定义。
  表内刻意不写任何注释（空间换时间的纯数据），一切解释都在本脚本里。

查表条目语义（display.c 运行时按此使用，全面覆写、无读改写）：
  HT1621 显存：地址 n = SEG n，nibble 的 D0..D3 = COM0..COM3。
  每位数字占两个 nibble（记 segX = 该位第 1 个 SEG，segY = 第 2 个）：
    segX: COM0=A, COM1=B, COM2=F, COM3=G
    segY: COM0=C, COM1=D, COM2=E, COM3=DP（个位该位为 NC）
  每条目 2 字节 {segX, segY}，就是该字形在这两个 nibble 里的直接内容；
  渲染时 6 个 nibble 一律从 00H 开始拼：三个字符各写各的两个 nibble
  （每位数字独占一对 nibble，互不重叠），segY 的 bit3（DP）由显示缓冲
  区 bit6 另行置位/清零，与字形无关。
  三个位置的条目内容完全相同（面板接线均匀），仍按位置各存一份：
  运行时免做"位置 -> SEG"换算，也为将来非均匀接线留余地。
"""

# ---- 段到位的映射：与 7seg-charset.html 的统一约定一致 ----
# bit0..bit6 = a..g；bit7 = dp 只在文档里表达字形用，本表不涉及。
BIT = {s: 1 << i for i, s in enumerate('abcdefg')}

# ---- 字形定义：字符 -> 点亮的段集合（严格抄自 7seg-charset.html 的 FONT 表）----
# None = 七段无法表示（K/M/V/W/X，含斜线笔画），统一回落 FALLBACK 字形。
# 想改某个字形：改这里对应的段集合，重跑脚本即可，display.c 其它代码不用动。
FONT = {
    '0': 'abcdef',
    '1': 'bc',
    '2': 'abdeg',
    '3': 'abcdg',
    '4': 'bcfg',
    '5': 'acdfg',
    '6': 'acdefg',   # 顶横保留：省去则与小写 b 冲突
    '7': 'abc',
    '8': 'abcdefg',
    '9': 'abcdfg',   # 底横保留：无底横变体分给 Q
    'A': 'abcefg',
    'B': 'cdefg',    # 小写 b 形
    'C': 'deg',      # 小写 c 形
    'D': 'bcdeg',    # 小写 d 形
    'E': 'adefg',
    'F': 'aefg',
    'G': 'acdef',    # 形似去掉中横的 6
    'H': 'bcefg',
    'I': 'acg',      # 小写 i 形：a 作点，c g 作竖画
    'J': 'bcde',
    'K': None,
    'L': 'def',
    'M': None,
    'N': 'ceg',      # 小写 n 形
    'O': 'cdeg',     # 小写 o 形，与数字 0 区分
    'P': 'abefg',
    'Q': 'abcfg',    # 无底横，与 9 区分
    'R': 'eg',       # 小写 r 形
    'S': 'acdfg',    # 与 5 同形
    'T': 'defg',     # 小写 t 形
    'U': 'bcdef',
    'V': None,       # 借 U 形无法与 U 区分，不采用
    'W': None,
    'X': None,
    'Y': 'bcdfg',    # 小写 y 形：比 9 少顶横
    'Z': 'abdeg',    # 与 2 同形
}
FALLBACK = 'd'       # 无法表示的字符 -> '_'：仅点亮底段 d

# ---- 位置 -> SEG 接线（hardware-definition.h 的 COM/SEG 表）----
# 显示缓冲区下标 0=个位 1=十位 2=百位（从右到左）；值 = (segX, segY)。
POS_SEG = {0: (4, 5), 1: (2, 3), 2: (0, 1)}

# 码值 0..35 的字符次序（显示缓冲区低 6 位存的就是这个码值）
CHARS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'


def glyph_mask(ch):
    """字符 -> 段位集（bit0..bit6 = a..g）"""
    segs = FONT[ch]
    if segs is None:
        segs = FALLBACK
    m = 0
    for s in segs:
        m |= BIT[s]
    return m


def table_entry(ch):
    """字符 -> 2 字节条目 {segX, segY}（该字形 nibble 的直接内容）"""
    m = glyph_mask(ch)
    seg_x = ((1 if m & BIT['a'] else 0) | (2 if m & BIT['b'] else 0) |
             (4 if m & BIT['f'] else 0) | (8 if m & BIT['g'] else 0))
    seg_y = ((1 if m & BIT['c'] else 0) | (2 if m & BIT['d'] else 0) |
             (4 if m & BIT['e'] else 0))
    return (seg_x, seg_y)


def main():
    assert len(FONT) == 36, 'FONT 必须恰好 36 个字符'
    assert set(FONT) == set(CHARS), 'FONT 字符集必须等于 0-9 A-Z'

    rows = [table_entry(c) for c in CHARS]

    # stdout：纯 C 数据，无任何注释（约定：表不带注释，解释都在本脚本）
    print('unsigned char code g_7segtab[3][36][2] =')
    print('{')
    for pos in range(3):
        print('    {')
        for r in rows:
            print('        {0x%02X, 0x%02X},' % r)
        print('    },')
    print('};')

    # stderr：校验摘要（不混入重定向的 C 代码）
    import sys
    for pos in range(3):
        print('pos %d -> SEG%d/SEG%d' % ((pos,) + POS_SEG[pos]), file=sys.stderr)
    print('table: 3 x 36 x 2 = %d bytes (code)' % (3 * 36 * 2), file=sys.stderr)
    for probe in '018K':
        print("spot '%s': mask=0x%02X entry=%s"
              % (probe, glyph_mask(probe), rows[CHARS.index(probe)]),
              file=sys.stderr)


if __name__ == '__main__':
    main()
