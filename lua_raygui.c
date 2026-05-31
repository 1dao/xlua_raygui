// ===================== 【顺序绝对不能错】宏定义放最前面 =====================
#define SUPPORT_MODULE_RMODELS    0
#define SUPPORT_MODULE_RAUDIO     0
#define SUPPORT_CAMERA_SYSTEM     0
#define SUPPORT_GESTURES_SYSTEM   0
#define SUPPORT_SCREEN_CAPTURE    0
#define SUPPORT_GIF_RECORDING     0

// ===================== 然后再包含头文件 =====================
#include "../raylib/src/raylib.h"
#include "../raygui/src/raygui.h"

#include <stdint.h>
#include <string.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define TEXT_BUF_SINGLE  4096
#define TEXT_BUF_MULTI   16384
#define FONT_PATH_MAX    1024
#define FONT_CODEPOINT_MAX 8192
#define FONT_UNICODE_MAX 0x110000
#define FONT_CODEPOINT_BITS ((FONT_UNICODE_MAX + 7) / 8)
#define FONT_TEXT_CACHE_SIZE 256

typedef struct FontTextCacheEntry {
    uint64_t hash;
    size_t length;
} FontTextCacheEntry;

static char g_buf_single[TEXT_BUF_SINGLE] = {0};
static char g_buf_multi[TEXT_BUF_MULTI]   = {0};

static Font g_font = {0};
static char g_font_path[FONT_PATH_MAX] = {0};
static int g_font_size = 0;
static int g_font_codepoints[FONT_CODEPOINT_MAX] = {0};
static int g_font_codepoint_count = 0;
static unsigned char g_font_codepoint_bits[FONT_CODEPOINT_BITS] = {0};
static FontTextCacheEntry g_font_text_cache[FONT_TEXT_CACHE_SIZE] = {0};
static int g_font_text_cache_next = 0;

static uint64_t font_hash_text(const char *text, size_t *length) {
    uint64_t hash = 1469598103934665603ULL;
    size_t len = 0;

    if (text) {
        while (text[len] != '\0') {
            hash ^= (unsigned char)text[len];
            hash *= 1099511628211ULL;
            len++;
        }
    }

    *length = len;
    return hash;
}

static bool font_text_cache_has(uint64_t hash, size_t length) {
    for (int i = 0; i < FONT_TEXT_CACHE_SIZE; i++) {
        if (g_font_text_cache[i].hash == hash && g_font_text_cache[i].length == length) {
            return true;
        }
    }
    return false;
}

static void font_text_cache_put(uint64_t hash, size_t length) {
    g_font_text_cache[g_font_text_cache_next].hash = hash;
    g_font_text_cache[g_font_text_cache_next].length = length;
    g_font_text_cache_next = (g_font_text_cache_next + 1) % FONT_TEXT_CACHE_SIZE;
}

static void font_text_cache_clear(void) {
    memset(g_font_text_cache, 0, sizeof(g_font_text_cache));
    g_font_text_cache_next = 0;
}

static void font_mark_codepoint(int codepoint) {
    if (codepoint >= 0 && codepoint < FONT_UNICODE_MAX) {
        g_font_codepoint_bits[codepoint >> 3] |= (unsigned char)(1u << (codepoint & 7));
    }
}

static bool font_has_codepoint(int codepoint) {
    if (codepoint < 0 || codepoint >= FONT_UNICODE_MAX) {
        return false;
    }
    return (g_font_codepoint_bits[codepoint >> 3] & (unsigned char)(1u << (codepoint & 7))) != 0;
}

static bool font_add_codepoint(int codepoint) {
    if (codepoint <= 0 || font_has_codepoint(codepoint)) return false;
    if (g_font_codepoint_count >= FONT_CODEPOINT_MAX) return false;
    g_font_codepoints[g_font_codepoint_count++] = codepoint;
    font_mark_codepoint(codepoint);
    return true;
}

static void font_rebuild_codepoint_bits(void) {
    memset(g_font_codepoint_bits, 0, sizeof(g_font_codepoint_bits));
    for (int i = 0; i < g_font_codepoint_count; i++) {
        font_mark_codepoint(g_font_codepoints[i]);
    }
}

static void font_seed_ascii(void) {
    for (int cp = 32; cp <= 126; cp++) font_add_codepoint(cp);
}

static bool font_add_text(const char *text, bool *overflow) {
    if (!text) return false;
    if (overflow) *overflow = false;

    bool changed = false;
    int byteCount = 0;
    for (int i = 0; text[i] != '\0'; i += byteCount) {
        int codepoint = GetCodepointNext(text + i, &byteCount);
        if (byteCount <= 0) byteCount = 1;
        if (codepoint >= 32 && !font_has_codepoint(codepoint)) {
            if (g_font_codepoint_count >= FONT_CODEPOINT_MAX) {
                if (overflow) *overflow = true;
            } else {
                changed |= font_add_codepoint(codepoint);
            }
        }
    }
    return changed;
}

static bool font_reload(void) {
    if (g_font_path[0] == '\0' || g_font_size <= 0 || g_font_codepoint_count <= 0) {
        return false;
    }

    Font font = LoadFontEx(g_font_path, g_font_size, g_font_codepoints, g_font_codepoint_count);
    if (font.texture.id == 0 || font.glyphCount <= 0 || font.baseSize != g_font_size) {
        if (font.texture.id != 0) UnloadFont(font);
        return false;
    }

    if (g_font.texture.id != 0) UnloadFont(g_font);
    g_font = font;
    GuiSetFont(g_font);
    return true;
}

static bool font_ensure_text(const char *text) {
    if (!text || text[0] == '\0') return true;

    size_t length = 0;
    uint64_t hash = font_hash_text(text, &length);
    if (font_text_cache_has(hash, length)) return true;

    int oldCount = g_font_codepoint_count;
    bool overflow = false;
    bool changed = font_add_text(text, &overflow);
    if (overflow) {
        g_font_codepoint_count = oldCount;
        font_rebuild_codepoint_bits();
        font_text_cache_clear();
        return false;
    }

    if (changed) {
        if (!font_reload()) {
            g_font_codepoint_count = oldCount;
            font_rebuild_codepoint_bits();
            font_text_cache_clear();
            return false;
        }
    }

    font_text_cache_put(hash, length);
    return true;
}

static void font_reset(void) {
    if (g_font.texture.id != 0) {
        UnloadFont(g_font);
        g_font = (Font){0};
    }
    g_font_path[0] = '\0';
    g_font_size = 0;
    g_font_codepoint_count = 0;
    memset(g_font_codepoint_bits, 0, sizeof(g_font_codepoint_bits));
    font_text_cache_clear();
}

//============================================================================
// 窗口渲染
//============================================================================
static int l_init(lua_State *L) {
    int w = (int)luaL_checkinteger(L, 1);
    int h = (int)luaL_checkinteger(L, 2);
    const char *title = luaL_checkstring(L, 3);
    InitWindow(w, h, title);
    SetTargetFPS(60);
    GuiLoadStyleDefault();
    // 初始化默认样式，防止错位
    GuiSetStyle(DEFAULT, TEXT_PADDING, 4);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    return 0;
}

static int l_close(lua_State *L) {
    (void)L;
    font_reset();
    CloseWindow();
    return 0;
}

static int l_should_close(lua_State *L) {
    lua_pushboolean(L, WindowShouldClose());
    return 1;
}

// 用于跨帧追踪鼠标是否悬停在任意交互 UI 领空内
static bool g_mouse_over_ui_current = false;
static bool g_mouse_over_ui_last    = false;
static int l_begin(lua_State *L) {
    (void)L;
    g_mouse_over_ui_last = g_mouse_over_ui_current;
    g_mouse_over_ui_current = false;
    
    BeginDrawing();
    ClearBackground((Color){24, 24, 24, 255});
    
    return 0;
}

static int l_finish(lua_State *L) {
    (void)L;
    EndDrawing();
    return 0;
}

// 辅助检测函数：只要有一个控件判定鼠标在内，当前帧就上报锁定
static void check_mouse_hover(Rectangle r) {
    if (CheckCollisionPointRec(GetMousePosition(), r)) {
        g_mouse_over_ui_current = true;
    }
}

//============================================================================
// 基础控件
//============================================================================
static int l_button(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    check_mouse_hover(r);
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    // // Debug: print first 3 bytes of text
    // printf("DEBUG: button text bytes: 0x%02X 0x%02X 0x%02X\n",
    //        (unsigned char)text[0], (unsigned char)text[1], (unsigned char)text[2]);
    lua_pushboolean(L, GuiButton(r, text));
    return 1;
}

static int l_label(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    GuiLabel(r, text);
    return 0;
}

static int l_checkbox(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    bool checked = lua_toboolean(L, 6);
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    GuiCheckBox(r, text, &checked);
    lua_pushboolean(L, checked);
    return 1;
}

static int l_slider(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    float min = (float)lua_tonumber(L,5);
    float max = (float)lua_tonumber(L,6);
    float val = (float)lua_tonumber(L,7);
    GuiSlider(r, NULL, NULL, &val, min, max);
    lua_pushnumber(L, val);
    return 1;
}

static int l_progressbar(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    float val = (float)lua_tonumber(L,5);
    float min = (float)lua_tonumber(L,6);
    float max = (float)lua_tonumber(L,7);
    GuiProgressBar(r, NULL, NULL, &val, min, max);
    return 0;
}

//============================================================================
// 基础控件 - 单行输入框（双返回值，全接管，完美中途插入与单光标移动）
//============================================================================
static int g_cursor_single = 0;
static int g_cursor_multi  = 0;
static int l_textbox(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1), lua_tonumber(L,2), lua_tonumber(L,3), lua_tonumber(L,4)};
    const char *text_from_lua = luaL_checkstring(L, 5);
    bool editMode = lua_toboolean(L, 6);

    strncpy(g_buf_single, text_from_lua, TEXT_BUF_SINGLE-1);
    g_buf_single[TEXT_BUF_SINGLE - 1] = '\0';
    int len = strlen(g_buf_single);

    // 1. 鼠标点击判定：完全自主控制焦点切换与光标初始定位
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(GetMousePosition(), r)) {
            if (!editMode) {
                editMode = true;
                g_cursor_single = len; // 首次聚焦，默认将光标移到文本末尾
            }
        } else {
            editMode = false;
        }
    }

    // 安全边界检查，防止外部数据导致光标越界
    if (g_cursor_single > len) g_cursor_single = len;
    if (g_cursor_single < 0) g_cursor_single = 0;

    // 2. 处于聚焦状态下，由 C 层全权负责文本插入、删除与光标左右移动
    if (editMode) {
        // A. 捕获键盘流及中文 IME 输入，并精准插入到当前光标所在位置
        int cp;
        while ((cp = GetCharPressed()) > 0) {
            char utf8[5] = {0};
            int bytes = 0;
            if (cp < 0x80) { utf8[0] = cp; bytes = 1; }
            else if (cp < 0x800) { utf8[0] = (cp >> 6) | 0xC0; utf8[1] = (cp & 0x3F) | 0x80; bytes = 2; }
            else if (cp < 0x10000) { utf8[0] = (cp >> 12) | 0xE0; utf8[1] = ((cp >> 6) & 0x3F) | 0x80; utf8[2] = (cp & 0x3F) | 0x80; bytes = 3; }
            else if (cp < 0x200000) { utf8[0] = (cp >> 18) | 0xF0; utf8[1] = ((cp >> 12) & 0x3F) | 0x80; utf8[2] = ((cp >> 6) & 0x3F) | 0x80; utf8[3] = (cp & 0x3F) | 0x80; bytes = 4; }

            if (bytes > 0 && (len + bytes < TEXT_BUF_SINGLE - 1)) {
                // 将光标后面的文本后移，空出位置给新输入的字
                memmove(g_buf_single + g_cursor_single + bytes, g_buf_single + g_cursor_single, len - g_cursor_single + 1);
                memcpy(g_buf_single + g_cursor_single, utf8, bytes);
                g_cursor_single += bytes; // 光标随字无缝后移！
                len += bytes;
            }
        }

        // B. 处理左移方向键 (Arrow Left) -> 跨越完整的 UTF-8 字符
        if (IsKeyPressed(KEY_LEFT)) {
            if (g_cursor_single > 0) {
                int prev = g_cursor_single - 1;
                while (prev > 0 && (g_buf_single[prev] & 0xC0) == 0x80) {
                    prev--;
                }
                g_cursor_single = prev;
            }
        }

        // C. 处理右移方向键 (Arrow Right) -> 跨越完整的 UTF-8 字符
        if (IsKeyPressed(KEY_RIGHT)) {
            if (g_cursor_single < len) {
                int next = g_cursor_single + 1;
                while (next < len && (g_buf_single[next] & 0xC0) == 0x80) {
                    next++;
                }
                g_cursor_single = next;
            }
        }

        // D. 处理退格键 (Backspace) -> 安全往前删除中英文字符
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (g_cursor_single > 0) {
                int prev = g_cursor_single - 1;
                while (prev > 0 && (g_buf_single[prev] & 0xC0) == 0x80) {
                    prev--;
                }
                int del_bytes = g_cursor_single - prev;
                memmove(g_buf_single + prev, g_buf_single + g_cursor_single, len - g_cursor_single + 1);
                g_cursor_single = prev;
                len -= del_bytes;
            }
        }

        // E. 处理删除键 (Delete) -> 安全往后删除中英文字符
        if (IsKeyPressed(KEY_DELETE)) {
            if (g_cursor_single < len) {
                int next = g_cursor_single + 1;
                while (next < len && (g_buf_single[next] & 0xC0) == 0x80) {
                    next++;
                }
                int del_bytes = next - g_cursor_single;
                memmove(g_buf_single + g_cursor_single, g_buf_single + next, len - next + 1);
                len -= del_bytes;
            }
        }

        // F. 按回车确认完成输入，自动释放状态
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            editMode = false;
        }
    }

    font_ensure_text(g_buf_single);

    // 3. 渲染部分：让 RayGUI 做纯粹的背景与文字被动渲染（固定传 false，彻底杜绝双光标重影现象）
    int oldAlign = GuiGetStyle(DEFAULT, TEXT_ALIGNMENT);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);

    if (editMode) GuiSetState(STATE_FOCUSED);
    GuiTextBox(r, g_buf_single, TEXT_BUF_SINGLE, false); 
    if (editMode) GuiSetState(STATE_NORMAL);

    // 4. 自主绘制唯一样式完美的高亮激活框与随动光标
    if (editMode) {
        Color activeColor = GetColor(GuiGetStyle(TEXTBOX, BORDER_COLOR_FOCUSED));
        DrawRectangleLinesEx(r, 1, activeColor);

        // 完美测算从开头到当前光标处的子字符串的像素实际宽度
        char tmp[TEXT_BUF_SINGLE];
        memcpy(tmp, g_buf_single, g_cursor_single);
        tmp[g_cursor_single] = '\0';

        Font font = GuiGetFont();
        float fontSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
        float padding = GuiGetStyle(DEFAULT, TEXT_PADDING);
        Vector2 textSize = MeasureTextEx(font, tmp, fontSize, 1);

        float cursor_x = r.x + padding + textSize.x + 2;
        float cursor_y = r.y + r.height/2;

        if (cursor_x < r.x + r.width - padding) {
            DrawLineV(
                (Vector2){ cursor_x, cursor_y - fontSize/2 },
                (Vector2){ cursor_x, cursor_y + fontSize/2 },
                activeColor
            );
        }
    }

    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, oldAlign);

    lua_pushstring(L, g_buf_single);
    lua_pushboolean(L, editMode);
    return 2; 
}

//============================================================================
// 基础控件 - 多行输入框（双返回值，全接管，支持完整的上下左右方向键跨行跳转）
//============================================================================
static int l_textbox_multi(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1), lua_tonumber(L,2), lua_tonumber(L,3), lua_tonumber(L,4)};
    const char *text_from_lua = luaL_checkstring(L, 5);
    bool editMode = lua_toboolean(L, 6);

    strncpy(g_buf_multi, text_from_lua, TEXT_BUF_MULTI-1);
    g_buf_multi[TEXT_BUF_MULTI - 1] = '\0';
    int len = strlen(g_buf_multi);

    // 1. 鼠标点击判定
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(GetMousePosition(), r)) {
            if (!editMode) {
                editMode = true;
                g_cursor_multi = len; // 首次聚焦，将光标放最末尾
            }
        } else {
            editMode = false;
        }
    }

    if (g_cursor_multi > len) g_cursor_multi = len;
    if (g_cursor_multi < 0) g_cursor_multi = 0;

    // 2. 键盘流控制及完整的上下左右核心处理机制
    if (editMode) {
        // A. 捕获常规打字及多汉字 IME 确认输入，中途插入到光标所在处
        int cp;
        while ((cp = GetCharPressed()) > 0) {
            char utf8[5] = {0};
            int bytes = 0;
            if (cp < 0x80) { utf8[0] = cp; bytes = 1; }
            else if (cp < 0x800) { utf8[0] = (cp >> 6) | 0xC0; utf8[1] = (cp & 0x3F) | 0x80; bytes = 2; }
            else if (cp < 0x10000) { utf8[0] = (cp >> 12) | 0xE0; utf8[1] = ((cp >> 6) & 0x3F) | 0x80; utf8[2] = (cp & 0x3F) | 0x80; bytes = 3; }
            else if (cp < 0x200000) { utf8[0] = (cp >> 18) | 0xF0; utf8[1] = ((cp >> 12) & 0x3F) | 0x80; utf8[2] = ((cp >> 6) & 0x3F) | 0x80; utf8[3] = (cp & 0x3F) | 0x80; bytes = 4; }

            if (bytes > 0 && (len + bytes < TEXT_BUF_MULTI - 1)) {
                memmove(g_buf_multi + g_cursor_multi + bytes, g_buf_multi + g_cursor_multi, len - g_cursor_multi + 1);
                memcpy(g_buf_multi + g_cursor_multi, utf8, bytes);
                g_cursor_multi += bytes;
                len += bytes;
            }
        }

        // B. 处理左移键 (Arrow Left)
        if (IsKeyPressed(KEY_LEFT)) {
            if (g_cursor_multi > 0) {
                int prev = g_cursor_multi - 1;
                while (prev > 0 && (g_buf_multi[prev] & 0xC0) == 0x80) {
                    prev--;
                }
                g_cursor_multi = prev;
            }
        }

        // C. 处理右移键 (Arrow Right)
        if (IsKeyPressed(KEY_RIGHT)) {
            if (g_cursor_multi < len) {
                int next = g_cursor_multi + 1;
                while (next < len && (g_buf_multi[next] & 0xC0) == 0x80) {
                    next++;
                }
                g_cursor_multi = next;
            }
        }

        // D. 【新增核心】处理上移键 (Arrow Up) —— 高精确跨行往上迁移
        if (IsKeyPressed(KEY_UP)) {
            int line_start = g_cursor_multi;
            while (line_start > 0 && g_buf_multi[line_start - 1] != '\n') {
                line_start--;
            }
            int col = g_cursor_multi - line_start; // 计算光标在当前行的字节偏离量

            if (line_start > 0) { // 存在上一行
                int prev_line_end = line_start - 1;
                int prev_line_start = prev_line_end;
                while (prev_line_start > 0 && g_buf_multi[prev_line_start - 1] != '\n') {
                    prev_line_start--;
                }
                int prev_line_len = prev_line_end - prev_line_start;
                if (col > prev_line_len) {
                    g_cursor_multi = prev_line_end;
                } else {
                    g_cursor_multi = prev_line_start + col;
                    // 对齐到合法的 UTF-8 字符起始字节
                    while (g_cursor_multi > prev_line_start && (g_buf_multi[g_cursor_multi] & 0xC0) == 0x80) {
                        g_cursor_multi--;
                    }
                }
            }
        }

        // E. 【新增核心】处理下移键 (Arrow Down) —— 高精确跨行往下迁移
        if (IsKeyPressed(KEY_DOWN)) {
            int line_start = g_cursor_multi;
            while (line_start > 0 && g_buf_multi[line_start - 1] != '\n') {
                line_start--;
            }
            int col = g_cursor_multi - line_start;

            int line_end = g_cursor_multi;
            while (g_buf_multi[line_end] && g_buf_multi[line_end] != '\n') {
                line_end++;
            }

            if (g_buf_multi[line_end] == '\n') { // 存在下一行
                int next_line_start = line_end + 1;
                int next_line_end = next_line_start;
                while (g_buf_multi[next_line_end] && g_buf_multi[next_line_end] != '\n') {
                    next_line_end++;
                }
                int next_line_len = next_line_end - next_line_start;
                if (col > next_line_len) {
                    g_cursor_multi = next_line_end;
                } else {
                    g_cursor_multi = next_line_start + col;
                    while (g_cursor_multi > next_line_start && (g_buf_multi[g_cursor_multi] & 0xC0) == 0x80) {
                        g_cursor_multi--;
                    }
                }
            }
        }

        // F. 处理退格键 (Backspace) 中途安全向前删除
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (g_cursor_multi > 0) {
                int prev = g_cursor_multi - 1;
                while (prev > 0 && (g_buf_multi[prev] & 0xC0) == 0x80) {
                    prev--;
                }
                int del_bytes = g_cursor_multi - prev;
                memmove(g_buf_multi + prev, g_buf_multi + g_cursor_multi, len - g_cursor_multi + 1);
                g_cursor_multi = prev;
                len -= del_bytes;
            }
        }

        // G. 处理删除键 (Delete) 中途安全向后删除
        if (IsKeyPressed(KEY_DELETE)) {
            if (g_cursor_multi < len) {
                int next = g_cursor_multi + 1;
                while (next < len && (g_buf_multi[next] & 0xC0) == 0x80) {
                    next++;
                }
                int del_bytes = next - g_cursor_multi;
                memmove(g_buf_multi + g_cursor_multi, g_buf_multi + next, len - next + 1);
                len -= del_bytes;
            }
        }

        // H. 处理多行换行（按回车在任意光标处切开文本并添加 \n）
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            if (len < TEXT_BUF_MULTI - 2) {
                memmove(g_buf_multi + g_cursor_multi + 1, g_buf_multi + g_cursor_multi, len - g_cursor_multi + 1);
                g_buf_multi[g_cursor_multi] = '\n';
                g_cursor_multi++;
                len++;
            }
        }
    }

    font_ensure_text(g_buf_multi);

    // 3. 渲染干净的多行文本框框
    int oldWrap = GuiGetStyle(DEFAULT, TEXT_WRAP_MODE);
    int oldAlign = GuiGetStyle(DEFAULT, TEXT_ALIGNMENT);
    GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);

    GuiTextBox(r, g_buf_multi, TEXT_BUF_MULTI, false); 

    // 4. 绘制多行模式下的激活外框与精准定位的光标（基于光标所在的当前行独立进行像素测量）
    if (editMode) {
        Color activeColor = GetColor(GuiGetStyle(TEXTBOX, BORDER_COLOR_FOCUSED));
        DrawRectangleLinesEx(r, 1, activeColor);

        Font font = GuiGetFont();
        float fontSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
        float padding = GuiGetStyle(DEFAULT, TEXT_PADDING);

        // 计算当前光标正处于第几行，以及当前行在全局文本里的起始字节偏离值
        int line_count_before = 0;
        int current_line_start = 0;
        for (int i = 0; i < g_cursor_multi && g_buf_multi[i]; i++) {
            if (g_buf_multi[i] == '\n') {
                line_count_before++;
                current_line_start = i + 1;
            }
        }

        // 完美截取当前行文本直到光标切面处的子字符串
        char cur_line_sub[TEXT_BUF_MULTI];
        int sub_len = g_cursor_multi - current_line_start;
        if (sub_len < 0) sub_len = 0;
        memcpy(cur_line_sub, g_buf_multi + current_line_start, sub_len);
        cur_line_sub[sub_len] = '\0';

        Vector2 subSize = MeasureTextEx(font, cur_line_sub, fontSize, 1);

        float cursor_x = r.x + padding + subSize.x + 2;
        float cursor_y = r.y + padding + line_count_before * (fontSize + 4) + fontSize / 2;

        if (cursor_y < r.y + r.height - padding) {
            DrawLineV(
                (Vector2){ cursor_x, cursor_y - fontSize / 2 },
                (Vector2){ cursor_x, cursor_y + fontSize / 2 },
                activeColor
            );
        }
    }

    GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, oldWrap);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, oldAlign);

    lua_pushstring(L, g_buf_multi);
    lua_pushboolean(L, editMode);
    return 2;
}

//============================================================================
// 容器
//============================================================================
static int l_panel(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    GuiPanel(r, text);
    return 0;
}

static int l_group(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    GuiGroupBox(r, text);
    return 0;
}

static int l_window(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    GuiWindowBox(r, text);
    lua_pushboolean(L, false);
    return 1;
}

static int l_dropdown(lua_State *L) {
    Rectangle r = {
        (float)lua_tonumber(L,1),
        (float)lua_tonumber(L,2),
        (float)lua_tonumber(L,3),
        (float)lua_tonumber(L,4)
    };

    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    int active = (int)lua_tointeger(L,6)-1;
    bool editMode = lua_toboolean(L,7);
    if (active < 0) active = 0;
    bool pressed = GuiDropdownBox(r, text, &active, editMode);
    if (pressed)
        editMode = !editMode;

    lua_pushinteger(L, (lua_Integer)(active + 1));
    lua_pushboolean(L, editMode);
    return 2;
}

//============================================================================
// 高级控件 - 滚动列表视图（1-based 索引自适应，完美支持鼠标悬停检测）
//============================================================================
static int l_listview(lua_State *L) {
    Rectangle r = {
        (float)lua_tonumber(L, 1),
        (float)lua_tonumber(L, 2),
        (float)lua_tonumber(L, 3),
        (float)lua_tonumber(L, 4)
    };
    const char *text = luaL_checkstring(L, 5);
    font_ensure_text(text);
    
    // 1. 别忘了我们之前加的鼠标悬停 UI 检测，让它也支持这个控件
    if (CheckCollisionPointRec(GetMousePosition(), r)) {
        g_mouse_over_ui_current = true; 
    }

    // 2. 转换 Lua 的 1-based 选定索引为 C 的 0-based 索引
    int active_idx = (int)lua_tointeger(L, 6) - 1;
    if (active_idx < 0) active_idx = 0;

    // 3. 接收当前的滚动条位置（RayGUI 内部用来控制长列表滚动）
    int scroll_idx = (int)lua_tointeger(L, 7);

    // 4. 调用 RayGUI 原生列表控件（注意：最后两个参数必须是 32位 int 指针）
    GuiListView(r, text, &scroll_idx, &active_idx);

    // 5. 将更新后的选定索引（变回 1-based）和最新的滚动条位置返回给 Lua
    lua_pushinteger(L, (lua_Integer)(active_idx + 1));
    lua_pushinteger(L, (lua_Integer)scroll_idx);
    return 2;
}

static int l_is_mouse_over_ui(lua_State *L) {
    lua_pushboolean(L, g_mouse_over_ui_last || g_mouse_over_ui_current);
    return 1;
}

static int l_set_style(lua_State *L) {
    int control = luaL_checkinteger(L, 1);
    int prop = luaL_checkinteger(L, 2);
    int value = luaL_checkinteger(L, 3);
    GuiSetStyle(control, prop, value);
    return 0;
}

static int l_load_font(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int fontSize = (int)luaL_checkinteger(L, 2);
    const char *charset = luaL_optstring(L, 3, NULL);

    font_reset();
    strncpy(g_font_path, path, FONT_PATH_MAX - 1);
    g_font_path[FONT_PATH_MAX - 1] = '\0';
    g_font_size = fontSize;
    font_seed_ascii();
    font_add_text(charset, NULL);

    if (!font_reload()) {
        g_font_path[0] = '\0';
        g_font_size = 0;
        g_font_codepoint_count = 0;
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

//============================================================================
// 注册
//============================================================================
static const luaL_Reg raygui_lib[] = {
    {"init",            l_init},
    {"close",           l_close},
    {"should_close",    l_should_close},
    {"begin",           l_begin},
    {"finish",          l_finish},
    {"button",          l_button},
    {"label",           l_label},
    {"checkbox",        l_checkbox},
    {"slider",          l_slider},
    {"progressbar",     l_progressbar},
    {"textbox",         l_textbox},
    {"textbox_multi",   l_textbox_multi},
    {"panel",           l_panel},
    {"group",           l_group},
    {"window",          l_window},
    {"dropdown",        l_dropdown},
    {"listview",        l_listview},
    {"is_mouse_over_ui",l_is_mouse_over_ui},
    {"set_style",       l_set_style},
    {"load_font",       l_load_font},
    {NULL, NULL}
};

static void register_consts(lua_State *L) {
    lua_pushinteger(L, 0); lua_setfield(L, -2, "DEFAULT");
    lua_pushinteger(L, 14); lua_setfield(L, -2, "TEXT_ALIGNMENT");
    lua_pushinteger(L, 16); lua_setfield(L, -2, "TEXT_SIZE");
    lua_pushinteger(L, 13); lua_setfield(L, -2, "TEXT_PADDING");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "TEXT_ALIGN_LEFT");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "TEXT_ALIGN_CENTER");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "TEXT_ALIGN_RIGHT");
}

int luaopen_raygui(lua_State *L) {
    luaL_newlib(L, raygui_lib);
    register_consts(L);
    return 1;
}
