-- raygui 测试脚本
---@type raygui
local raygui = require("raygui")

local function script_dir()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    source = source:gsub("\\", "/")
    return source:match("^(.*)/") or "."
end

-- 初始化窗口
raygui.init(910, 870, "RayGUI Lua Test")

-- 加载中文字体
local font_path = script_dir() .. "/fonts/NotoSansSC-Regular.otf"
assert(raygui.load_font(font_path, 20), "Font load failed...")

-- ===== 应用现代 UI 风格（6选1，不用的注释掉） =====
-- require("styles.dark").apply(raygui)   -- Dark: VS Code 风格深色
-- require("styles.cyber").apply(raygui)  -- Cyber: 赛博朋克霓虹
require("styles.candy").apply(raygui)  -- Candy: 糖果暖色调
-- require("styles.nord").apply(raygui)   -- ★ Nord: 北极蓝灰 — 冷静高级
-- require("styles.soft").apply(raygui)   -- Soft: 清新柔光 — 现代干净
-- (不加载任何风格 = raygui 默认风格)

raygui.set_style(raygui.DEFAULT, raygui.TEXT_SIZE, 20)
raygui.set_style(raygui.DEFAULT, raygui.TEXT_PADDING, 4)
raygui.set_style(raygui.DEFAULT, raygui.TEXT_ALIGNMENT, raygui.TEXT_ALIGN_LEFT)

-- 状态变量
local button_clicked = 0
local checkbox_checked = false
local show_password = false      -- “显示密码”复选框独立状态
local slider_value = 50.0
local progress_value = 0
local text_input = "Hello RayGUI!"
local text_input_edit = false
local text_multi = "This is a multi-line\ntext box that supports\nword wrapping."
local text_multi_edit = false
local dropdown_selected = 1
local dropdown_open = false
local list_selected = 1
local list_scroll = 0
local confirm_open = false        -- 确认对话框是否弹出
local confirm_result = ""         -- 上次对话框的选择结果（用于显示）

-- 背包（游戏物品栏）状态：用 emoji 当物品图标
local bag_open = false
local bag_selected = nil          -- 选中的格子序号（0 起）
local bag_items = {
    {emoji="gem",    name="宝石",   count=12},
    {emoji="key",    name="钥匙",   count=3},
    {emoji="book",   name="技能书", count=1},
    {emoji="coffee", name="药水",   count=5},
    {emoji="fire",   name="火符",   count=9},
    {emoji="star",   name="星石",   count=99},
    {emoji="heart",  name="红心",   count=20},
    {emoji="bulb",   name="灯泡",   count=2},
    {emoji="rocket", name="火箭",   count=1},
    {emoji="crown",  name="王冠",   count=1},
    {emoji="trophy", name="奖杯",   count=1},
    {emoji="shield", name="护盾",   count=4},
    {emoji="hammer", name="锤子",   count=7},
    {emoji="map",    name="地图",   count=2},
    {emoji="gift",   name="礼包",   count=6},
    {emoji="music",  name="乐符",   count=8},
}

-- 预加载贴图（raygui.init 已创建窗口/GL 上下文，此时可加载纹理）
local emoji_tex = raygui.load_texture(script_dir() .. "/emoji_atlas.png")
-- emoji 图集元数据：cell/cols 为图集网格，index 是 名字->序号（与 emoji_atlas.png 对应）。
-- 改图集时用 gen_emoji_atlas.py 重新生成 PNG，脚本会把这张表打印出来，覆盖粘贴即可。
local EMOJI = { cell = 72, cols = 12, count = 132, index = {
    save=0, open=1, folder=2, file=3, new=4, edit=5, delete=6, add=7,
    remove=8, copy=9, cut=10, search=11, settings=12, tools=13, refresh=14, sync=15,
    undo=16, redo=17, check=18, close=19, ok=20, cancel=21, warning=22, info=23,
    question=24, exclamation=25, home=26, star=27, heart=28, bookmark=29, pin=30, tag=31,
    flag=32, bell=33, bell_off=34, lock=35, unlock=36, key=37, eye=38, up=39,
    down=40, left=41, right=42, back=43, toparr=44, play=45, pause=46, stop=47,
    record=48, next=49, prev=50, forward=51, rewind=52, sound=53, mute=54, mic=55,
    music=56, mail=57, chat=58, phone=59, mobile=60, upload=61, download=62, link=63,
    attach=64, chart=65, trending=66, calendar=67, clock=68, hourglass=69, print=70, computer=71,
    keyboard=72, battery=73, camera=74, video=75, image=76, money=77, card=78, cart=79,
    gift=80, trophy=81, crown=82, gem=83, fire=84, sparkles=85, sun=86, moon=87,
    cloud=88, rain=89, snow=90, rainbow=91, zap=92, droplet=93, rocket=94, bulb=95,
    book=96, memo=97, globe=98, package=99, party=100, balloon=101, cake=102, coffee=103,
    thumbs_up=104, thumbs_down=105, ok_hand=106, clap=107, wave=108, point_right=109, pray=110, muscle=111,
    smile=112, grin=113, joy=114, wink=115, cool=116, think=117, cry=118, angry=119,
    love=120, robot=121, user=122, users=123, shield=124, target=125, location=126, map=127,
    game=128, hundred=129, hammer=130, puzzle=131,
} }
print(("[纹理] emoji_tex=%s, emoji 数=%d")
    :format(tostring(emoji_tex), EMOJI.count))

-- 把 emoji（名字或数字序号）解析成图集里的源矩形 (sx, sy, sw, sh)
local function emoji_src(key)
    local idx = type(key) == "string" and EMOJI.index[key] or key
    assert(idx, "未知 emoji: " .. tostring(key))
    local c, cell = EMOJI.cols, EMOJI.cell
    return (idx % c) * cell, (idx // c) * cell, cell, cell   -- 网格定位
end

-- 在 (x,y) 处画一个 size×size 的 emoji（key 可用名字 "save" 或数字序号）
local function draw_emoji(key, x, y, size)
    if not emoji_tex then return end
    local sx, sy, sw, sh = emoji_src(key)
    raygui.draw_texture_ex(emoji_tex, sx, sy, sw, sh, x, y, size, size)
end

-- 组合按钮：彩色 emoji 贴图(左) + 文字(右)。
-- 思路：raygui 按钮只能画自己的文字，所以先画一个【文字左对齐、左内边距留空】的
-- 普通按钮，再把 emoji 贴图叠加在按钮左侧 —— 点击逻辑仍用按钮自身的返回值。
-- key 可用名字（如 "save"）或数字序号。
local function emoji_button(x, y, w, h, key, text)
    local esize = h - 12   -- emoji 边长，略小于按钮高度
    -- 文字左对齐，并把左内边距撑到 emoji 宽度，让文字排在 emoji 右边
    raygui.set_style(raygui.DEFAULT, raygui.TEXT_ALIGNMENT, raygui.TEXT_ALIGN_LEFT)
    raygui.set_style(raygui.DEFAULT, raygui.TEXT_PADDING, esize + 10)
    local clicked = raygui.button(x, y, w, h, text)
    -- 恢复本 demo 的默认样式（左对齐、padding=4）
    raygui.set_style(raygui.DEFAULT, raygui.TEXT_PADDING, 4)
    raygui.set_style(raygui.DEFAULT, raygui.TEXT_ALIGNMENT, raygui.TEXT_ALIGN_LEFT)
    -- 在按钮左侧、垂直居中叠加 emoji 贴图（自动缩放）
    draw_emoji(key, x + 6, y + (h - esize) / 2, esize)
    return clicked
end

-- 游戏背包窗口（模态）：标题栏 + ✖ 关闭 + 物品格子(emoji 当图标) + 右侧详情
local function draw_backpack()
    local bw, bh = 520, 410
    local bx, by = (910 - bw) // 2, (870 - bh) // 2   -- 居中

    -- 窗口框（GuiWindowBox = 面板 + 标题栏 + 右上角 ✖；点 ✖ 返回 true）
    if raygui.window(bx, by, bw, bh, "背包 Backpack") then
        bag_open = false
    end

    -- 物品格子：5 列 × 4 行
    local cols, rows = 5, 4
    local ss, gap = 64, 10
    local gx, gy = bx + 16, by + 44
    for i = 0, cols * rows - 1 do
        local cx = gx + (i % cols) * (ss + gap)
        local cy = gy + (i // cols) * (ss + gap)
        -- 空文字按钮当格子，点击即选中
        if raygui.button(cx, cy, ss, ss, "") then
            bag_selected = i
        end
        local it = bag_items[i + 1]
        if it then
            draw_emoji(it.emoji, cx + 12, cy + 4, 40)                 -- 物品图标
            raygui.label(cx + 6, cy + 44, ss - 12, 18, "x" .. it.count)  -- 数量
            if bag_selected == i then
                raygui.draw_icon(112, cx + ss - 20, cy + 2, 1, 40, 200, 80, 255)  -- ✓ 选中
            end
        end
    end

    -- 右侧详情栏
    local dx = bx + cols * (ss + gap) + 24
    local dy = by + 44
    raygui.label(dx, dy, 140, 24, "—— 详情 ——")
    local sel = bag_selected and bag_items[bag_selected + 1]
    if sel then
        draw_emoji(sel.emoji, dx + 28, dy + 36, 64)
        raygui.label(dx, dy + 112, 140, 24, "名称: " .. sel.name)
        raygui.label(dx, dy + 140, 140, 24, "数量: " .. sel.count)
        if raygui.button(dx, dy + 176, 110, 34, "使用 x1") then
            sel.count = sel.count - 1
            if sel.count <= 0 then
                table.remove(bag_items, bag_selected + 1)
                bag_selected = nil
            end
        end
    else
        raygui.label(dx, dy + 40, 140, 24, "点格子选择物品")
    end

    raygui.label(bx + 16, by + bh - 30, bw - 32, 22,
        "点格子选中物品，右侧可“使用”；点右上角 ✖ 关闭背包")
end


-- 主循环
while not raygui.should_close() do
    raygui.begin()

    -- 背景面板
    raygui.panel(0, 0, 910, 870, "")

    -- dropdown 展开 / 对话框 / 背包弹出时锁定其它控件（避免点击穿透），最后再置顶绘制它们
    if dropdown_open or confirm_open or bag_open then raygui.lock() end

    -- ============ 第一行 · 左：基础控件 ============
    raygui.group(15, 15, 430, 240, "基础控件 Basic")

    if raygui.button(35, 55, 110, 38, "点我") then
        button_clicked = button_clicked + 1
    end
    raygui.label(160, 60, 250, 28, "Clicked: " .. button_clicked .. " times")

    -- 复选框文字单独用 label 画（避免左对齐时 raygui 把复选框文字甩到框左边、跑出界）
    checkbox_checked = raygui.checkbox(35, 112, 24, 24, "", checkbox_checked)
    raygui.label(70, 113, 120, 28, "Toggle Me")
    raygui.label(235, 112, 200, 28, "状态: " .. (checkbox_checked and "ON" or "OFF"))

    raygui.label(35, 158, 70, 28, "Slider")
    slider_value = raygui.slider(115, 158, 215, 28, 0, 100, slider_value, 0, 100)
    raygui.label(340, 158, 60, 28, string.format("%.0f", slider_value))

    raygui.label(35, 205, 75, 28, "Progress")
    raygui.progressbar(115, 207, 215, 20, progress_value, 0, 100)
    progress_value = (progress_value + 0.5) % 100

    -- ============ 第一行 · 右：文本控件 ============
    raygui.group(460, 15, 435, 240, "文本控件 Text")

    raygui.label(478, 56, 70, 28, "Text")
    text_input, text_input_edit = raygui.textbox(556, 52, 320, 38, text_input, text_input_edit)

    -- 多行框：长按退格连删、内容过多时裁剪并跟随光标滚动（已在 raygui.dll 内修复）
    raygui.label(478, 110, 70, 28, "Multi")
    text_multi, text_multi_edit = raygui.textbox_multi(556, 105, 320, 130, text_multi, text_multi_edit)

    -- ============ 第二行 · 左：选择控件（列表 + 下拉）============
    raygui.group(15, 270, 430, 250, "选择控件 Selection")

    raygui.label(35, 306, 80, 24, "List")
    list_selected, list_scroll = raygui.listview(
        35, 334, 185, 150,
        "选项一;选项二;选项三;选项四;选项五;选项六;选项七;选项八",
        list_selected, list_scroll)
    raygui.label(35, 490, 185, 22, "索引: " .. tostring(list_selected))

    raygui.label(235, 306, 120, 24, "Dropdown")
    -- “Selected” 标签放这里（dropdown 框体在循环末尾置顶绘制；展开时列表会盖住它）
    raygui.label(235, 384, 195, 24, "Selected: " .. dropdown_selected)

    -- ============ 第二行 · 右：容器 / 鼠标 ============
    raygui.group(460, 270, 435, 250, "容器 / 鼠标 Containers")

    raygui.label(478, 304, 380, 24, "分组框 + 控件:")
    raygui.group(478, 334, 400, 96, "Group Box")
    raygui.button(495, 364, 165, 40, "#131# Button 1")
    -- 彩色 emoji + 文字 组合按钮：emoji 贴图(名字 "open" 📂) 在左，文字在右
    if emoji_button(672, 364, 180, 40, "open", "打开 2") then print("打开 2 被点击") end

    raygui.label(478, 448, 90, 24, "Mouse:")
    raygui.label(575, 448, 300, 24, raygui.is_mouse_over_ui() and "悬停在 UI 上" or "未悬停")

    -- 打开背包：弹出"游戏物品栏"窗口（实现见循环末尾 draw_backpack）
    if emoji_button(478, 480, 200, 34, "package", "打开背包") then bag_open = true end

    -- ============ 第三行：图标 #iconID# + 单色符号 emoji ============
    raygui.group(15, 535, 880, 160, "图标 #iconID# / 单色符号 emoji")

    -- 控件内图标缩放（默认1=16px；设为2=32px）
    raygui.set_icon_scale(2)
    -- 一整行图标按钮（#图标ID# 语法把内置图标嵌进文字，与字体无关）
    if raygui.button(35,  572, 120, 36, "#131# 播放") then print("播放被点击") end
    if raygui.button(160, 572, 120, 36, "#132# 暂停") then print("暂停被点击") end
    if raygui.button(285, 572, 120, 36, "#133# 停止") then print("停止被点击") end
    if raygui.button(410, 572, 120, 36, "#5# 打开")   then print("打开被点击") end
    if raygui.button(535, 572, 120, 36, "#6# 保存")   then print("保存被点击") end
    if raygui.button(660, 572, 120, 36, "#9# 删除")   then confirm_open = true end  -- 弹确认框
    -- 第二行：设置按钮 + 图标标签 + 带图标的复选框
    raygui.button(35, 614, 120, 36, "#141# 设置")
    raygui.label(165, 618, 230, 28, "#186# 你好 RayGUI!")          -- ♥ + 文字
    -- 复选框文字单独用 label 画（label 同样支持 #iconID#，眼睛图标随之渲染）
    show_password = raygui.checkbox(405, 616, 24, 24, "", show_password)
    raygui.label(437, 618, 180, 28, "#44# 显示密码")
    raygui.set_icon_scale(1)

    -- 字体自带的单色符号 emoji（raylib 文字引擎只画单色轮廓，忽略彩色表）
    raygui.label(35, 658, 845, 24,
        "单色符号 emoji（字体自带）: ⭐ ❤ ☀ ☁ ⚡ ⚙ ✈ ✉ ✂ ✏ ▶ ♪ ☎ ☂ ⚓ ❗")

    -- ============ 第四行：彩色 emoji（贴图）/ 组合按钮 / 对话框 ============
    raygui.group(15, 710, 880, 145, "彩色 emoji（贴图）/ 组合按钮 / 对话框")

    raygui.label(35, 745, 580, 24,
        "彩色 emoji：共 " .. EMOJI.count .. " 个，可按名字取用 (save/open/close/search…)")
    if confirm_result ~= "" then
        raygui.label(640, 745, 250, 24, "上次对话框: " .. confirm_result)
    end

    -- emoji + 文字 组合按钮（用名字引用 emoji，无需记数字）
    if emoji_button(35,  775, 150, 36, "save",     "保存") then print("保存") end
    if emoji_button(195, 775, 150, 36, "close",    "关闭") then print("关闭") end
    if emoji_button(355, 775, 150, 36, "search",   "搜索") then print("搜索") end
    if emoji_button(515, 775, 150, 36, "settings", "设置") then print("设置") end
    if emoji_button(675, 775, 150, 36, "delete",   "删除") then print("删除") end

    -- 图集采样示例：从网格里取前 26 个 emoji 排成一行（draw_emoji 自动按网格定位）
    if emoji_tex then
        for i = 0, 25 do
            draw_emoji(i, 35 + i * 30, 822, 24)
        end
    end

    -- ============ 置顶层：dropdown / 背包 / 对话框（最后绘制，正确 z 序）============
    -- 分两层：dropdown 在中间层；背包/对话框是更高的模态层。
    local modal = confirm_open or bag_open
    if not modal then raygui.unlock() end   -- 无模态时解锁，dropdown 可正常交互

    -- dropdown 放最后画，展开列表盖在主控件之上
    dropdown_selected, dropdown_open = raygui.dropdown(
        235, 334, 195, 38,
        "Option 1;Option 2;Option 3;Option 4", dropdown_selected, dropdown_open)

    if modal then raygui.unlock() end       -- 解锁，让模态窗口本身可交互

    -- 游戏背包窗口（模态，盖在最上层）
    if bag_open then draw_backpack() end

    -- 模态确认对话框：点 #iconID# “删除”弹出。-1=未点 0=✖ 1=取消 2=确定
    if confirm_open then
        local res = raygui.messagebox(295, 360, 320, 156,
            "确认删除", "确定要删除吗？\n此操作不可撤销。", "取消;确定")
        if res >= 0 then
            confirm_open = false
            confirm_result = (res == 2) and "确定 ✅" or "取消 ✖"
            print(res == 2 and "用户点击了【确定】" or "用户点击了【取消】")
        end
    end

    raygui.finish()
end

if emoji_tex then
    raygui.unload_texture(emoji_tex)
end

raygui.close()
print("Test completed!")
