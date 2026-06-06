#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成彩色 emoji 图集：
  - 读取系统 Segoe UI Emoji 字体 (C:\\Windows\\Fonts\\seguiemj.ttf)
  - 把下面 EMOJIS 列表里的 emoji 逐个栅格化，按网格排进一张 PNG
  - 并把「可直接粘贴进 demo」的内联元数据表 (local EMOJI = {...}) 打印到 stdout

元数据（cell/cols/count/名字->序号）现在内联在 test_ui.lua / xraygui_main.lua 顶部，
不再单独写 .lua 文件。改了图集后，把脚本打印的那段覆盖 demo 里的 local EMOJI = {...}。

用法:  python gen_emoji_atlas.py
想增删 emoji：改 EMOJIS 列表后重新运行即可。
渲染不出来的（字体里没有的）会自动跳过并打印警告，图集与映射表保持紧凑无空洞。
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = r"C:\Windows\Fonts\seguiemj.ttf"
CELL   = 72     # 每格像素（源尺寸；绘制时可任意缩放）
COLS   = 12     # 网格列数
RENDER = 64     # 栅格化字号
OUT_PNG = "emoji_atlas.png"

# (name, emoji)  —— 顺序即索引顺序，常用的放前面
EMOJIS = [
    # --- 文件/编辑 动作 ---
    ("save","💾"),("open","📂"),("folder","📁"),("file","📄"),("new","🆕"),
    ("edit","✏️"),("delete","🗑️"),("add","➕"),("remove","➖"),("copy","📋"),
    ("cut","✂️"),("search","🔍"),("settings","⚙️"),("tools","🔧"),("refresh","🔄"),
    ("sync","🔃"),("undo","↩️"),("redo","↪️"),
    # --- 状态/提示 ---
    ("check","✅"),("close","❌"),("ok","✔️"),("cancel","✖️"),("warning","⚠️"),
    ("info","ℹ️"),("question","❓"),("exclamation","❗"),
    # --- 收藏/标记 ---
    ("home","🏠"),("star","⭐"),("heart","❤️"),("bookmark","🔖"),("pin","📌"),
    ("tag","🏷️"),("flag","🚩"),("bell","🔔"),("bell_off","🔕"),
    # --- 安全 ---
    ("lock","🔒"),("unlock","🔓"),("key","🔑"),("eye","👁️"),
    # --- 方向 ---
    ("up","⬆️"),("down","⬇️"),("left","⬅️"),("right","➡️"),("back","🔙"),("toparr","🔝"),
    # --- 媒体 ---
    ("play","▶️"),("pause","⏸️"),("stop","⏹️"),("record","⏺️"),("next","⏭️"),
    ("prev","⏮️"),("forward","⏩"),("rewind","⏪"),("sound","🔊"),("mute","🔇"),
    ("mic","🎤"),("music","🎵"),
    # --- 通讯 ---
    ("mail","✉️"),("chat","💬"),("phone","📞"),("mobile","📱"),("upload","📤"),
    ("download","📥"),("link","🔗"),("attach","📎"),
    # --- 数据/时间 ---
    ("chart","📊"),("trending","📈"),("calendar","📅"),("clock","⏰"),("hourglass","⏳"),
    # --- 设备 ---
    ("print","🖨️"),("computer","💻"),("keyboard","⌨️"),("battery","🔋"),("camera","📷"),
    ("video","📹"),("image","🖼️"),
    # --- 购物/奖励 ---
    ("money","💰"),("card","💳"),("cart","🛒"),("gift","🎁"),("trophy","🏆"),
    ("crown","👑"),("gem","💎"),
    # --- 自然/天气 ---
    ("fire","🔥"),("sparkles","✨"),("sun","☀️"),("moon","🌙"),("cloud","☁️"),
    ("rain","🌧️"),("snow","❄️"),("rainbow","🌈"),("zap","⚡"),("droplet","💧"),
    # --- 杂项 ---
    ("rocket","🚀"),("bulb","💡"),("book","📖"),("memo","📝"),("globe","🌐"),
    ("package","📦"),("party","🎉"),("balloon","🎈"),("cake","🎂"),("coffee","☕"),
    # --- 手势 ---
    ("thumbs_up","👍"),("thumbs_down","👎"),("ok_hand","👌"),("clap","👏"),("wave","👋"),
    ("point_right","👉"),("pray","🙏"),("muscle","💪"),
    # --- 表情 ---
    ("smile","😀"),("grin","😁"),("joy","😂"),("wink","😉"),("cool","😎"),
    ("think","🤔"),("cry","😢"),("angry","😠"),("love","😍"),("robot","🤖"),
    # --- 补充高频（凑满网格）---
    ("user","👤"),("users","👥"),("shield","🛡️"),("target","🎯"),("location","📍"),
    ("map","🗺️"),("game","🎮"),("hundred","💯"),("hammer","🔨"),("puzzle","🧩"),
]

font = ImageFont.truetype(FONT_PATH, RENDER)

def rasterize(ch):
    """渲染一个 emoji，返回裁剪后的 RGBA 图；渲染不出来返回 None。"""
    for s in (ch, ch + "️"):          # 不行就补一个 VS16 变体选择符再试
        scratch = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
        ImageDraw.Draw(scratch).text((10, 10), s, font=font, embedded_color=True)
        bb = scratch.getbbox()
        if bb:
            return scratch.crop(bb)
    return None

rendered = []   # (name, glyph_image)
for name, ch in EMOJIS:
    g = rasterize(ch)
    if g is None:
        print("  跳过(字体无此字形):", name, repr(ch))
        continue
    rendered.append((name, g))

count = len(rendered)
rows = (count + COLS - 1) // COLS
atlas = Image.new("RGBA", (COLS * CELL, rows * CELL), (0, 0, 0, 0))
maxd = CELL - 6
for idx, (name, g) in enumerate(rendered):
    if max(g.size) > maxd:                  # 缩放以适配格子
        r = maxd / max(g.size)
        g = g.resize((max(1, int(g.width * r)), max(1, int(g.height * r))), Image.LANCZOS)
    cx = (idx % COLS) * CELL + (CELL - g.width) // 2
    cy = (idx // COLS) * CELL + (CELL - g.height) // 2
    atlas.alpha_composite(g, (cx, cy))

atlas.save(OUT_PNG)
print(f"saved {OUT_PNG}: {atlas.size}, {count} emoji, {COLS}x{rows} grid")

# 打印「可直接粘贴进 demo」的内联元数据表（元数据内联在 test_ui.lua / xraygui_main.lua，
# 不再单独写 .lua 文件）。改了图集后，把下面这段覆盖 demo 顶部的 local EMOJI = {...} 即可。
print()
print("-- ===== 复制下面整段，覆盖 demo 顶部的 local EMOJI = {...} =====")
print(f"local EMOJI = {{ cell = {CELL}, cols = {COLS}, count = {count}, index = {{")
line = "   "
for idx, (name, _) in enumerate(rendered):
    line += f" {name}={idx},"
    if (idx + 1) % 8 == 0:
        print(line)
        line = "   "
if line.strip():
    print(line)
print("} }")
