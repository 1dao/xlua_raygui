---@meta
---@diagnostic disable: unreachable-code, unused-local
-- 上面的 ---@meta 告诉编辑器：这是一个纯元数据定义文件，不要执行它，也不要报“未导入”或“空函数”的警告。

-- ============================================================================
-- Runtime shim: Lua searches raygui.lua before raygui.dll/raygui.so.
-- Keep this metadata file require-able by forwarding to the native module.
-- ============================================================================
do
    local function load_native_raygui()
        local source = debug.getinfo(1, "S").source
        if source:sub(1, 1) == "@" then
            source = source:sub(2)
        end

        local dir = source:match("^(.*[/\\])") or ""
        dir = dir:gsub("\\", "/")

        local ext = (package.config:sub(1, 1) == "\\") and "dll" or "so"
        local path = dir .. "raygui." .. ext
        local loader, err = package.loadlib(path, "luaopen_raygui")
        if not loader then
            error(("failed to load native raygui module from %q: %s"):format(path, err), 2)
        end

        return loader()
    end

    return load_native_raygui()
end

---@class raygui
local raygui = {}

-- ============================================================================
-- 1. 常量补全定义 (对应 C 层的 register_consts)
-- ============================================================================

---@type integer
raygui.DEFAULT = 0

---@type integer
raygui.TEXT_ALIGNMENT = 14

---@type integer
raygui.TEXT_SIZE = 16

---@type integer
raygui.TEXT_PADDING = 13

---@type integer
raygui.TEXT_ALIGN_LEFT = 0

---@type integer
raygui.TEXT_ALIGN_CENTER = 1

---@type integer
raygui.TEXT_ALIGN_RIGHT = 2


-- ============================================================================
-- 2. 函数补全与参数提示定义 (对应你之前修好的双返回值 C 函数)
-- ============================================================================

---初始化窗口
---@param width integer 窗口宽度
---@param height integer 窗口高度
---@param title string 窗口标题
function raygui.init(width, height, title) end

---开始绘制帧
function raygui.begin() end

---结束绘制帧
function raygui.finish() end

---是否应该关闭窗口
---@return boolean
function raygui.should_close() end

---检测鼠标是否悬停在交互 UI 上
---@return boolean
function raygui.is_mouse_over_ui() end

---绘制标准按钮
---@param x number
---@param y number
---@param width number
---@param height number
---@param text string
---@return boolean pressed 是否被点击
function raygui.button(x, y, width, height, text) end

---单行文本输入框
---@param x number
---@param y number
---@param width number
---@param height number
---@param text string 当前文本内容
---@param editMode boolean 是否处于编辑状态
---@return string text 最新文本内容
---@return boolean editMode 最新编辑状态
function raygui.textbox(x, y, width, height, text, editMode) end

---多行文本输入框
---@param x number
---@param y number
---@param width number
---@param height number
---@param text string 当前文本内容
---@param editMode boolean 是否处于编辑状态
---@return string text 最新文本内容
---@return boolean editMode 最新编辑状态
function raygui.textbox_multi(x, y, width, height, text, editMode) end

---下拉选择框
---@param x number
---@param y number
---@param width number
---@param height number
---@param text string 用分号分隔的选项字符串 (例如 "Opt1;Opt2")
---@param selected integer 当前选中的 1-based 索引
---@param open boolean 当前下拉框是否展开
---@return integer selected 最新选中的索引
---@return boolean open 最新展开状态
function raygui.dropdown(x, y, width, height, text, selected, open) end

---滚动列表视图
---@param x number
---@param y number
---@param width number
---@param height number
---@param text string 用分号分隔的选项字符串
---@param selected integer 当前选中的 1-based 索引
---@param scroll integer 当前滚动条位置
---@return integer selected 最新选中的索引
---@return integer scroll 最新滚动条位置
function raygui.listview(x, y, width, height, text, selected, scroll) end

return raygui
