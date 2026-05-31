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
raygui.init(800, 600, "RayGUI Lua Test")

-- 加载中文字体
local font_path = script_dir() .. "/fonts/NotoSansSC-Regular.otf"
assert(raygui.load_font(font_path, 20), "Font load failed...")
raygui.set_style(raygui.DEFAULT, raygui.TEXT_SIZE, 20)
raygui.set_style(raygui.DEFAULT, raygui.TEXT_PADDING, 4)
raygui.set_style(raygui.DEFAULT, raygui.TEXT_ALIGNMENT, raygui.TEXT_ALIGN_CENTER) 

-- 状态变量
local button_clicked = 0
local checkbox_checked = false
local slider_value = 50.0
local progress_value = 0
local text_input = "Hello RayGUI!"
local text_input_edit = false
local text_multi = "This is a multi-line\ntext box that supports\nword wrapping."
local text_multi_edit = false
local dropdown_selected = 1
local dropdown_open = false


-- 主循环
while not raygui.should_close() do
    raygui.begin()
    
    -- 窗口标题
    -- raygui.window(10, 10, 780, 580, "Test Window")
    raygui.panel(10, 10, 780, 580, "")
    
    -- 按钮
    if raygui.button(20, 50, 120, 40, "点我") then
        button_clicked = button_clicked + 1
    end
    raygui.label(150, 60, 200, 30, "Clicked: " .. button_clicked .. " times")
    
    -- 复选框
    checkbox_checked = raygui.checkbox(20, 110, 150, 30, "Toggle Me", checkbox_checked)
    raygui.label(180, 115, 200, 30, "Checkbox is " .. (checkbox_checked and "ON" or "OFF"))

    -- 滑动条
    raygui.label(20, 160, 100, 30, "Slider:")
    -- slider_value = raygui.slider(120, 155, 200, 30, nil, nil, slider_value, 0, 100)
    slider_value = raygui.slider(120, 155, 200, 30, 0, 100, slider_value, 0, 100)
    raygui.label(330, 160, 100, 30, string.format("%.1f", slider_value))
        
    -- 进度条
    raygui.label(20, 210, 100, 30, "Progress:")
    raygui.progressbar(120, 205, 200, 30, progress_value, 0, 100)
    progress_value = (progress_value + 0.5) % 100

    -- 文本输入框
    raygui.label(20, 260, 100, 30, "Text Input:")
    text_input, text_input_edit = raygui.textbox(120, 255, 300, 40, text_input, text_input_edit)

    -- 多行文本框
    raygui.label(20, 310, 100, 30, "Multi-line:")
    text_multi, text_multi_edit = raygui.textbox_multi(120, 305, 300, 80, text_multi, text_multi_edit)

    -- 下拉框
    raygui.label(20, 410, 100, 30, "Dropdown:")
    dropdown_selected, dropdown_open = raygui.dropdown(120, 405, 200, 40, "Option 1;Option 2;Option 3;Option 4", dropdown_selected, dropdown_open)
    raygui.label(330, 410, 100, 30, "Selected: " .. dropdown_selected)

    -- 面板和分组
    raygui.panel(450, 50, 320, 200, "Panel")
    raygui.group(470, 80, 280, 150, "Group Box")
    raygui.label(490, 110, 240, 30, "Controls inside group")
    raygui.button(490, 150, 100, 35, "Button 1")
    raygui.button(610, 150, 100, 35, "Button 2")

    -- 鼠标检测
    raygui.label(450, 270, 150, 30, "Mouse:")
    if raygui.is_mouse_over_ui() then
        raygui.label(600, 270, 150, 30, "Mouse is down!")
    else
        raygui.label(600, 270, 150, 30, "Mouse is up")
    end

    -- =================================================================
    -- ✨ 渲染列表控件（选项之间必须严格使用分号 ; 分隔）
    -- =================================================================
    raygui.label(500, 310, 100, 30, "ListView 列表:")
    
    -- 传入：坐标、宽高、分号分隔的文本、当前选中项、当前滚动位置
    -- 返回：新选中项、新滚动位置
    list_selected, list_scroll = raygui.listview(
        500, 340, 240, 180, 
        "选项一;选项二;选项三;选项四;选项五;选项六;选项七;选项八", 
        list_selected, 
        list_scroll
    )
    
    -- 展示当前点中了谁
    raygui.label(500, 530, 200, 30, "当前选择索引: " .. list_selected)
        
    raygui.finish()
end

raygui.close()
print("Test completed!")
