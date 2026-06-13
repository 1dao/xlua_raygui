// ===================== 【顺序绝对不能错】宏定义放最前面 =====================
#define SUPPORT_MODULE_RMODELS    0
#define SUPPORT_MODULE_RAUDIO     0
#define SUPPORT_CAMERA_SYSTEM     0
#define SUPPORT_GESTURES_SYSTEM   0
#define SUPPORT_SCREEN_CAPTURE    0
#define SUPPORT_GIF_RECORDING     0
#define RAYGUI_SUPPORT_UTF8   // 核心！开启UTF-8支持（中文/Emoji必备）
#define RAYGUI_IMPLEMENTATION

// 跨平台导出宏（配合 -fvisibility=hidden 使用）
#if defined(_WIN32)
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

// ===================== 然后再包含头文件 =====================
#include "../raylib/src/raylib.h"
#include "../raygui/src/raygui.h"

// raygui 官方示例的应用内文件/目录选择对话框（拷贝自
// ../raygui/examples/custom_file_dialog/，include 路径已修）。
// 立即模式浮窗：file_dialog_open() 打开，每帧 file_dialog() 绘制并取状态。
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
// dll 压缩 upx --best --lzma raygui.dll

// 剪贴板探测：剪贴板里是图片等非文本时，GLFW 的 GetClipboardText 会失败并刷
// "Failed to convert clipboard to string" WARNING。粘贴前先用 Win32 探一下有无
// 文本格式，没有就静默跳过。windows.h 与 raylib 符号冲突（Rectangle/CloseWindow/
// DrawText…），故手工声明所需函数（user32/kernel32，MinGW 默认链接）。
#if defined(_WIN32)
__declspec(dllimport) int __stdcall IsClipboardFormatAvailable(unsigned int format);
__declspec(dllimport) int __stdcall OpenClipboard(void *hWndNewOwner);
__declspec(dllimport) int __stdcall CloseClipboard(void);
__declspec(dllimport) void * __stdcall GetClipboardData(unsigned int uFormat);
__declspec(dllimport) void * __stdcall GlobalLock(void *hMem);
__declspec(dllimport) int __stdcall GlobalUnlock(void *hMem);
__declspec(dllimport) size_t __stdcall GlobalSize(void *hMem);
#define WIN32_CF_TEXT        1
#define WIN32_CF_DIB         8
#define WIN32_CF_UNICODETEXT 13
static int clipboard_has_text(void) {
    return IsClipboardFormatAvailable(WIN32_CF_UNICODETEXT)
        || IsClipboardFormatAvailable(WIN32_CF_TEXT);
}
static int clipboard_has_image(void) {
    return IsClipboardFormatAvailable(WIN32_CF_DIB);
}
#else
static int clipboard_has_text(void) { return 1; }
static int clipboard_has_image(void) { return 0; }
#endif

// textbox_multi 编辑态里按 Ctrl+V 而剪贴板是图片（无文本）时置位；
// Lua 侧每帧用 take_pasted_image() 轮询取走（取走即清零）。
static int g_image_pasted = 0;

#define TEXT_BUF_SINGLE  4096
#define TEXT_BUF_MULTI   16384
#define FONT_PATH_MAX    1024
#define FONT_CODEPOINT_MAX 131071
#define FONT_UNICODE_MAX 0x110000
#define FONT_CODEPOINT_BITS ((FONT_UNICODE_MAX + 7) / 8)
#define FONT_TEXT_CACHE_SIZE 256
#define MAX_TEXTURES 256

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

static Texture g_textures[MAX_TEXTURES] = {0};
static bool  g_texture_used[MAX_TEXTURES] = {0};

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
// 纹理管理
//============================================================================
// 找一个空槽存放纹理；满了返回 0（不抛错，由调用方负责释放传入的纹理）
static int texture_alloc(Texture texture) {
    for (int i = 1; i < MAX_TEXTURES; i++) {
        if (!g_texture_used[i]) {
            g_textures[i] = texture;
            g_texture_used[i] = true;
            return i;
        }
    }
    return 0;
}

static void texture_free_all(void) {
    for (int i = 1; i < MAX_TEXTURES; i++) {
        if (g_texture_used[i]) {
            UnloadTexture(g_textures[i]);
            g_textures[i] = (Texture){0};
            g_texture_used[i] = false;
        }
    }
}

static Texture* texture_get(lua_State *L, int id) {
    if (id < 1 || id >= MAX_TEXTURES || !g_texture_used[id]) {
        luaL_error(L, "invalid texture id: %d", id);
    }
    return &g_textures[id];
}

static int l_load_texture(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    Texture tex = LoadTexture(path);
    if (tex.id == 0) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to load texture");
        return 2;
    }
    int id = texture_alloc(tex);
    if (id == 0) {
        UnloadTexture(tex);   // 槽位已满，立即释放刚加载的纹理，避免 GPU 资源泄漏
        lua_pushnil(L);
        lua_pushstring(L, "texture limit reached");
        return 2;
    }
    lua_pushinteger(L, id);
    lua_pushnil(L);
    return 2;
}

static int l_unload_texture(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    if (id >= 1 && id < MAX_TEXTURES && g_texture_used[id]) {
        UnloadTexture(g_textures[id]);
        g_textures[id] = (Texture){0};
        g_texture_used[id] = false;
    }
    return 0;
}

static int l_draw_texture(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_optnumber(L, 4, -1);
    float h = (float)luaL_optnumber(L, 5, -1);
    int tr = (int)luaL_optinteger(L, 6, 255);
    int tg = (int)luaL_optinteger(L, 7, 255);
    int tb = (int)luaL_optinteger(L, 8, 255);
    int ta = (int)luaL_optinteger(L, 9, 255);

    Texture *tex = texture_get(L, id);
    Color tint = { (unsigned char)tr, (unsigned char)tg, (unsigned char)tb, (unsigned char)ta };

    if (w < 0) w = (float)tex->width;
    if (h < 0) h = (float)tex->height;

    Rectangle dst = { x, y, w, h };
    Rectangle src = { 0, 0, (float)tex->width, (float)tex->height };
    Vector2 origin = { 0, 0 };
    DrawTexturePro(*tex, src, dst, origin, 0.0f, tint);
    return 0;
}

static int l_draw_texture_ex(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    float sx = (float)luaL_checknumber(L, 2);
    float sy = (float)luaL_checknumber(L, 3);
    float sw = (float)luaL_checknumber(L, 4);
    float sh = (float)luaL_checknumber(L, 5);
    float dx = (float)luaL_checknumber(L, 6);
    float dy = (float)luaL_checknumber(L, 7);
    float dw = (float)luaL_optnumber(L, 8, sw);
    float dh = (float)luaL_optnumber(L, 9, sh);
    int tr = (int)luaL_optinteger(L, 10, 255);
    int tg = (int)luaL_optinteger(L, 11, 255);
    int tb = (int)luaL_optinteger(L, 12, 255);
    int ta = (int)luaL_optinteger(L, 13, 255);

    Texture *tex = texture_get(L, id);
    Color tint = { (unsigned char)tr, (unsigned char)tg, (unsigned char)tb, (unsigned char)ta };

    Rectangle src = { sx, sy, sw, sh };
    Rectangle dst = { dx, dy, dw, dh };
    Vector2 origin = { 0, 0 };
    DrawTexturePro(*tex, src, dst, origin, 0.0f, tint);
    return 0;
}

//============================================================================
// 剪贴板图片：CF_DIB → RGBA → （必要时缩小）→ PNG 内存编码
//============================================================================
#if defined(_WIN32)
// 支持 24/32bpp 未压缩 DIB（BI_RGB / 标准掩码 BI_BITFIELDS，覆盖截图工具、
// 浏览器复制、QQ/微信等主流来源）；其余格式返回 NULL。长边超过 1568 像素时
// 等比缩小（Anthropic 推荐的图片上限，顺便压住 PNG/base64 体积）。
// 返回 RL_MALLOC 的 PNG 缓冲（调用方 MemFree），尺寸经 out_size/out_w/out_h。
static unsigned char *grab_clipboard_image_png(int *out_size, int *out_w, int *out_h) {
    *out_size = 0;
    if (!clipboard_has_image() || !OpenClipboard(NULL)) return NULL;

    unsigned char *png = NULL;
    void *hmem = GetClipboardData(WIN32_CF_DIB);
    unsigned char *p = hmem ? (unsigned char *)GlobalLock(hmem) : NULL;
    if (p) {
        size_t   total = GlobalSize(hmem);
        uint32_t hsz, comp;
        int32_t  w, hgt;
        uint16_t bpp;
        memcpy(&hsz,  p + 0,  4);          // BITMAPINFOHEADER（或 V4/V5）字段
        memcpy(&w,    p + 4,  4);
        memcpy(&hgt,  p + 8,  4);
        memcpy(&bpp,  p + 14, 2);
        memcpy(&comp, p + 16, 4);
        int top_down = hgt < 0;
        int height   = top_down ? -hgt : hgt;
        // 像素区偏移：头 + （仅 40 字节头的 BI_BITFIELDS 带 3 个掩码 DWORD）
        size_t off = (size_t)hsz + ((comp == 3 && hsz == 40) ? 12 : 0);

        if (total > 20 && hsz >= 40 && w > 0 && height > 0 && w < 32768 && height < 32768
            && (bpp == 24 || bpp == 32) && (comp == 0 || comp == 3)) {
            size_t stride = ((size_t)w * (bpp / 8) + 3) & ~(size_t)3;
            if (off + stride * (size_t)height <= total) {
                unsigned char *rgba = (unsigned char *)malloc((size_t)w * height * 4);
                if (rgba) {
                    int alpha_all_zero = 1;
                    for (int y = 0; y < height; y++) {
                        const unsigned char *row = p + off
                            + stride * (size_t)(top_down ? y : (height - 1 - y));
                        unsigned char *dst = rgba + (size_t)y * w * 4;
                        if (bpp == 32) {
                            for (int x = 0; x < w; x++) {       // BGRA → RGBA
                                dst[x*4+0] = row[x*4+2];
                                dst[x*4+1] = row[x*4+1];
                                dst[x*4+2] = row[x*4+0];
                                dst[x*4+3] = row[x*4+3];
                                if (row[x*4+3]) alpha_all_zero = 0;
                            }
                        } else {
                            for (int x = 0; x < w; x++) {       // BGR → RGBA
                                dst[x*4+0] = row[x*3+2];
                                dst[x*4+1] = row[x*3+1];
                                dst[x*4+2] = row[x*3+0];
                                dst[x*4+3] = 255;
                            }
                            alpha_all_zero = 0;
                        }
                    }
                    // 32bpp 常见坑：不少来源 alpha 通道全 0 表示“无 alpha”，
                    // 直接用会得到全透明图 → 按不透明处理。
                    if (alpha_all_zero) {
                        for (size_t i = 3; i < (size_t)w * height * 4; i += 4) rgba[i] = 255;
                    }

                    // 注意：rgba 用 malloc 分配，后续 ImageResize/UnloadImage 用
                    // RL_FREE 释放它 —— 仅在默认配置（RL_FREE==free）下成立。
                    Image img = { rgba, w, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                    int maxdim = (w > height) ? w : height;
                    if (maxdim > 1568) {
                        float k = 1568.0f / (float)maxdim;
                        ImageResize(&img, (int)(w * k), (int)(height * k));
                    }
                    int fsz = 0;
                    unsigned char *mem = ExportImageToMemory(img, ".png", &fsz);
                    UnloadImage(img);          // 释放（可能已被 resize 替换的）像素
                    if (mem && fsz > 0) {
                        png = mem; *out_size = fsz; *out_w = img.width; *out_h = img.height;
                    } else if (mem) {
                        MemFree(mem);
                    }
                }
            }
        }
        GlobalUnlock(hmem);
    }
    CloseClipboard();
    return png;
}
#else
static unsigned char *grab_clipboard_image_png(int *out_size, int *out_w, int *out_h) {
    (void)out_w; (void)out_h; *out_size = 0; return NULL;
}
#endif

// get_clipboard_image() -> png_bytes, w, h | nil  （剪贴板位图编码为 PNG）
static int l_get_clipboard_image(lua_State *L) {
    int size = 0, w = 0, h = 0;
    unsigned char *png = grab_clipboard_image_png(&size, &w, &h);
    if (!png) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char *)png, (size_t)size);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    MemFree(png);
    return 3;
}

// take_pasted_image() -> png_bytes, w, h | nil
// 输入框 Ctrl+V 粘贴图片的取走式轮询（无粘贴时返回 nil，开销仅一次旗标判断）。
static int l_take_pasted_image(lua_State *L) {
    if (!g_image_pasted) { lua_pushnil(L); return 1; }
    g_image_pasted = 0;
    return l_get_clipboard_image(L);
}

// load_texture_mem(png_bytes) -> id, w, h | nil, err  （内存 PNG → 纹理，缩略图用）
static int l_load_texture_mem(lua_State *L) {
    size_t n = 0;
    const char *data = luaL_checklstring(L, 1, &n);
    Image img = LoadImageFromMemory(".png", (const unsigned char *)data, (int)n);
    if (!img.data) { lua_pushnil(L); lua_pushstring(L, "decode failed"); return 2; }
    Texture tex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (tex.id == 0) { lua_pushnil(L); lua_pushstring(L, "texture upload failed"); return 2; }
    int id = texture_alloc(tex);
    if (id == 0) {
        UnloadTexture(tex);
        lua_pushnil(L); lua_pushstring(L, "texture limit reached");
        return 2;
    }
    lua_pushinteger(L, id);
    lua_pushinteger(L, tex.width);
    lua_pushinteger(L, tex.height);
    return 3;
}

static int l_draw_icon(lua_State *L) {
    int iconId  = (int)luaL_checkinteger(L, 1);
    int posX    = (int)luaL_checkinteger(L, 2);
    int posY    = (int)luaL_checkinteger(L, 3);
    int pixelSize = (int)luaL_checkinteger(L, 4);
    int tr = (int)luaL_optinteger(L, 5, 255);
    int tg = (int)luaL_optinteger(L, 6, 255);
    int tb = (int)luaL_optinteger(L, 7, 255);
    int ta = (int)luaL_optinteger(L, 8, 255);

    // 边界检查：GuiDrawIcon 不校验 iconId，越界会读到 guiIcons 数组之外的内存
    if (iconId < 0 || iconId >= RAYGUI_ICON_MAX_ICONS) {
        luaL_error(L, "invalid icon id: %d (valid range 0..%d)", iconId, RAYGUI_ICON_MAX_ICONS - 1);
    }

    Color color = { (unsigned char)tr, (unsigned char)tg, (unsigned char)tb, (unsigned char)ta };
    GuiDrawIcon(iconId, posX, posY, pixelSize, color);
    return 0;
}

static int l_set_icon_scale(lua_State *L) {
    int scale = (int)luaL_checkinteger(L, 1);
    GuiSetIconScale(scale);
    return 0;
}

static int l_load_style(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    if (!FileExists(path)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "style file not found");
        return 2;
    }
    GuiLoadStyle(path);
    // 风格文件可能内嵌字体并调用 GuiSetFont 覆盖我们加载的中文字体；
    // 重新应用已加载的字体，确保中文/Emoji 仍能正常显示
    if (g_font.texture.id != 0) {
        GuiSetFont(g_font);
    }
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return 2;
}

//============================================================================
// 全局锁定：让 dropdown/combobox 等"展开型"控件能画在最上层。
// 用法：控件展开时先 lock() 再绘制其它控件（被覆盖区域不会误响应点击），
// 然后 unlock() 并最后绘制该展开控件本身。
//============================================================================
static int l_lock(lua_State *L) {
    (void)L;
    GuiLock();
    return 0;
}

static int l_unlock(lua_State *L) {
    (void)L;
    GuiUnlock();
    return 0;
}

static int l_is_locked(lua_State *L) {
    lua_pushboolean(L, GuiIsLocked());
    return 1;
}

//============================================================================
// 3D 模型视图：把一个旋转的 3D 模型渲染到离屏纹理，再贴到面板的指定矩形内。
// 这样 3D 场景就能像普通控件一样嵌在 raygui 界面里。
//============================================================================
static RenderTexture2D g_model_rt = {0};
static int   g_model_rt_w = 0, g_model_rt_h = 0;
static Model g_model = {0};
static Texture2D g_model_tex = {0};
static bool  g_model_ready = false;

static void model_view_init(void) {
    if (g_model_ready) return;
    // 棋盘格纹理，贴到立方体上当作"模型贴图"
    Image chk = GenImageChecked(64, 64, 8, 8,
                                (Color){235, 235, 235, 255}, (Color){90, 140, 230, 255});
    g_model_tex = LoadTextureFromImage(chk);
    UnloadImage(chk);
    // 生成一个立方体网格模型，并把贴图挂到漫反射通道
    g_model = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));
    g_model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = g_model_tex;
    g_model_ready = true;
}

static void model_view_free(void) {
    if (!g_model_ready) return;
    UnloadModel(g_model);                 // 注意：会顺带卸载材质，但贴图是我们自己建的，单独卸
    UnloadTexture(g_model_tex);
    if (g_model_rt.id != 0) UnloadRenderTexture(g_model_rt);
    g_model_rt = (RenderTexture2D){0};
    g_model = (Model){0};
    g_model_tex = (Texture2D){0};
    g_model_rt_w = g_model_rt_h = 0;
    g_model_ready = false;
}

// model_view(x, y, w, h [, angle])
//   在 (x,y,w,h) 区域内显示一个自动旋转的 3D 立方体模型 + 地面网格。
//   不传 angle 则按时间自动旋转。
static int l_model_view(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    float angle = (float)luaL_optnumber(L, 5, GetTime() * 35.0);
    int iw = (int)w, ih = (int)h;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;

    model_view_init();

    // 离屏纹理尺寸跟随面板大小；变化时重建
    if (g_model_rt.id == 0 || g_model_rt_w != iw || g_model_rt_h != ih) {
        if (g_model_rt.id != 0) UnloadRenderTexture(g_model_rt);
        g_model_rt = LoadRenderTexture(iw, ih);
        g_model_rt_w = iw;
        g_model_rt_h = ih;
    }

    Camera3D cam = {0};
    cam.position   = (Vector3){ 5.0f, 4.0f, 5.0f };
    cam.target     = (Vector3){ 0.0f, 0.6f, 0.0f };
    cam.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy       = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    // 1) 把 3D 场景渲染到离屏纹理
    BeginTextureMode(g_model_rt);
        ClearBackground((Color){30, 32, 38, 255});
        BeginMode3D(cam);
            DrawGrid(10, 1.0f);
            DrawModelEx(g_model, (Vector3){0, 1, 0}, (Vector3){0, 1, 0}, angle,
                        (Vector3){1, 1, 1}, WHITE);
            DrawModelWiresEx(g_model, (Vector3){0, 1, 0}, (Vector3){0, 1, 0}, angle,
                             (Vector3){1, 1, 1}, (Color){0, 0, 0, 60});
        EndMode3D();
    EndTextureMode();

    // 2) 把离屏纹理贴回面板矩形（RenderTexture 的 y 是翻转的，源高取负）
    Rectangle src = { 0.0f, 0.0f, (float)iw, -(float)ih };
    Rectangle dst = { x, y, w, h };
    DrawTexturePro(g_model_rt.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
    return 0;
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
    texture_free_all();
    model_view_free();
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
    ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));  // follow the active theme

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
static int g_scroll_multi  = 0;   // 多行框纵向滚动（以"行"为单位）
static int g_sel_anchor    = -1;  // 多行框选区锚点（-1=无选区；否则与光标构成选区）

// 首次按下 + 长按时的系统级自动重复（退格/删除/方向键长按可连续触发）
static bool key_repeat(int key) {
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

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
        if (key_repeat(KEY_LEFT)) {
            if (g_cursor_single > 0) {
                int prev = g_cursor_single - 1;
                while (prev > 0 && (g_buf_single[prev] & 0xC0) == 0x80) {
                    prev--;
                }
                g_cursor_single = prev;
            }
        }

        // C. 处理右移方向键 (Arrow Right) -> 跨越完整的 UTF-8 字符
        if (key_repeat(KEY_RIGHT)) {
            if (g_cursor_single < len) {
                int next = g_cursor_single + 1;
                while (next < len && (g_buf_single[next] & 0xC0) == 0x80) {
                    next++;
                }
                g_cursor_single = next;
            }
        }

        // D. 处理退格键 (Backspace) -> 安全往前删除中英文字符
        if (key_repeat(KEY_BACKSPACE)) {
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
        if (key_repeat(KEY_DELETE)) {
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
// ── 多行框选区辅助 ──────────────────────────────────────────────────────────
static void tbm_sel_range(int *a, int *b) {
    int c = g_cursor_multi, s = g_sel_anchor;
    if (s < 0 || s == c) { *a = c; *b = c; return; }
    if (s < c) { *a = s; *b = c; } else { *a = c; *b = s; }
}
static bool tbm_has_sel(void) { int a, b; tbm_sel_range(&a, &b); return a != b; }
static void tbm_del_sel(int *len) {
    int a, b; tbm_sel_range(&a, &b);
    if (a == b) return;
    memmove(g_buf_multi + a, g_buf_multi + b, *len - b + 1);
    *len -= (b - a);
    g_cursor_multi = a;
    g_sel_anchor = -1;
}
// 把鼠标位置映射成 g_buf_multi 中的字节偏移（按 \n 分行 + 纵向滚动 + 逐字测宽）
static int tbm_pos_from_mouse(Rectangle r, float fontSize, float spacing, float padding, float lineH, int len) {
    Font font = GuiGetFont();
    Vector2 m = GetMousePosition();
    int target = g_scroll_multi + (int)((m.y - (r.y + padding)) / lineH);
    if (target < 0) target = 0;

    int line = 0, ls = 0, i = 0;
    for (; i <= len; i++) {
        if (i == len || g_buf_multi[i] == '\n') {
            if (line == target) break;
            line++; ls = i + 1;
        }
    }
    int le = i;                       // 行末（\n 或 len）
    if (line < target) ls = le;       // 点在最后一行之下

    float relx = m.x - (r.x + padding);
    if (relx <= 0) return ls;

    char tmp[TEXT_BUF_MULTI];
    int j = ls;
    while (j < le) {
        int nj = j + 1;
        while (nj < le && (g_buf_multi[nj] & 0xC0) == 0x80) nj++;   // 跨完整 UTF-8 字符
        int sub = nj - ls;
        memcpy(tmp, g_buf_multi + ls, sub); tmp[sub] = '\0';
        float w = MeasureTextEx(font, tmp, fontSize, spacing).x;
        if (w > relx) {
            int subj = j - ls;
            memcpy(tmp, g_buf_multi + ls, subj); tmp[subj] = '\0';
            float pw = MeasureTextEx(font, tmp, fontSize, spacing).x;
            return (relx < (pw + w) / 2.0f) ? j : nj;   // 取更近的字符边界
        }
        j = nj;
    }
    return le;
}

static int l_textbox_multi(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1), lua_tonumber(L,2), lua_tonumber(L,3), lua_tonumber(L,4)};
    const char *text_from_lua = luaL_checkstring(L, 5);
    bool editMode = lua_toboolean(L, 6);
    bool enter_submits = lua_toboolean(L, 7);   // optional: Enter=submit, Ctrl+Enter=newline
    bool submitted = false;

    strncpy(g_buf_multi, text_from_lua, TEXT_BUF_MULTI-1);
    g_buf_multi[TEXT_BUF_MULTI - 1] = '\0';
    int len = strlen(g_buf_multi);

    // 字体度量（鼠标定位与渲染共用）
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float spacing  = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    float padding  = (float)GuiGetStyle(DEFAULT, TEXT_PADDING);
    float lineH    = fontSize + 4.0f;

    // 1. 鼠标：点击定位光标并起选区；按住拖拽扩展选区
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(GetMousePosition(), r)) {
            editMode = true;
            g_cursor_multi = tbm_pos_from_mouse(r, fontSize, spacing, padding, lineH, len);
            g_sel_anchor = g_cursor_multi;
        } else {
            editMode = false;
            g_sel_anchor = -1;
        }
    } else if (editMode && IsMouseButtonDown(MOUSE_LEFT_BUTTON)
               && CheckCollisionPointRec(GetMousePosition(), r)) {
        g_cursor_multi = tbm_pos_from_mouse(r, fontSize, spacing, padding, lineH, len);  // 拖拽：锚点不动，选区扩展
    }

    if (g_cursor_multi > len) g_cursor_multi = len;
    if (g_cursor_multi < 0) g_cursor_multi = 0;
    if (g_sel_anchor > len) g_sel_anchor = len;

    // 2. 键盘流控制及完整的上下左右核心处理机制
    if (editMode) {
        bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);
        bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        // 快捷键：Ctrl+A 全选 / Ctrl+C 复制 / Ctrl+X 剪切 / Ctrl+V 粘贴
        if (ctrl) {
            if (IsKeyPressed(KEY_A)) { g_sel_anchor = 0; g_cursor_multi = len; }
            if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X)) {
                int a, b; tbm_sel_range(&a, &b);
                if (a == b) { a = 0; b = len; }                 // 无选区 → 整段
                char saved = g_buf_multi[b]; g_buf_multi[b] = '\0';
                SetClipboardText(g_buf_multi + a);
                g_buf_multi[b] = saved;
                if (IsKeyPressed(KEY_X) && tbm_has_sel()) tbm_del_sel(&len);
            }
            if (IsKeyPressed(KEY_V)) {
                if (clipboard_has_text()) {
                    const char *clip = GetClipboardText();
                    if (clip && clip[0]) {
                        if (tbm_has_sel()) tbm_del_sel(&len);
                        int cl = (int)strlen(clip);
                        if (len + cl < TEXT_BUF_MULTI - 1) {
                            memmove(g_buf_multi + g_cursor_multi + cl, g_buf_multi + g_cursor_multi, len - g_cursor_multi + 1);
                            memcpy(g_buf_multi + g_cursor_multi, clip, cl);
                            g_cursor_multi += cl; len += cl;
                        }
                    }
                } else if (clipboard_has_image()) {
                    g_image_pasted = 1;     // Lua 侧 take_pasted_image() 取走
                }
            }
        }

        // A. 捕获常规打字及多汉字 IME 确认输入，中途插入到光标所在处
        int cp;
        while ((cp = GetCharPressed()) > 0) {
            if (ctrl) continue;                     // Ctrl 组合不当字符输入
            if (tbm_has_sel()) tbm_del_sel(&len);   // 有选区：先删除选区
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
        if (key_repeat(KEY_LEFT)) {
            if (shift) { if (g_sel_anchor < 0) g_sel_anchor = g_cursor_multi; } else g_sel_anchor = -1;
            if (g_cursor_multi > 0) {
                int prev = g_cursor_multi - 1;
                while (prev > 0 && (g_buf_multi[prev] & 0xC0) == 0x80) {
                    prev--;
                }
                g_cursor_multi = prev;
            }
        }

        // C. 处理右移键 (Arrow Right)
        if (key_repeat(KEY_RIGHT)) {
            if (shift) { if (g_sel_anchor < 0) g_sel_anchor = g_cursor_multi; } else g_sel_anchor = -1;
            if (g_cursor_multi < len) {
                int next = g_cursor_multi + 1;
                while (next < len && (g_buf_multi[next] & 0xC0) == 0x80) {
                    next++;
                }
                g_cursor_multi = next;
            }
        }

        // D. 【新增核心】处理上移键 (Arrow Up) —— 高精确跨行往上迁移
        if (key_repeat(KEY_UP)) {
            if (shift) { if (g_sel_anchor < 0) g_sel_anchor = g_cursor_multi; } else g_sel_anchor = -1;
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
        if (key_repeat(KEY_DOWN)) {
            if (shift) { if (g_sel_anchor < 0) g_sel_anchor = g_cursor_multi; } else g_sel_anchor = -1;
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

        // F. 处理退格键 (Backspace)：有选区先删选区，否则向前删一字
        if (key_repeat(KEY_BACKSPACE)) {
            if (tbm_has_sel()) { tbm_del_sel(&len); }
            else if (g_cursor_multi > 0) {
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

        // G. 处理删除键 (Delete)：有选区先删选区，否则向后删一字
        if (key_repeat(KEY_DELETE)) {
            if (tbm_has_sel()) { tbm_del_sel(&len); }
            else if (g_cursor_multi < len) {
                int next = g_cursor_multi + 1;
                while (next < len && (g_buf_multi[next] & 0xC0) == 0x80) {
                    next++;
                }
                int del_bytes = next - g_cursor_multi;
                memmove(g_buf_multi + g_cursor_multi, g_buf_multi + next, len - next + 1);
                len -= del_bytes;
            }
        }

        // H. 回车：enter_submits 时普通回车=提交（不插换行），Ctrl+回车=换行；
        //    否则（默认）回车总是插入换行。
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (enter_submits && !ctrl) {
                submitted = true;   // caller sends; no newline inserted
            } else if (len < TEXT_BUF_MULTI - 2) {
                if (tbm_has_sel()) tbm_del_sel(&len);
                memmove(g_buf_multi + g_cursor_multi + 1, g_buf_multi + g_cursor_multi, len - g_cursor_multi + 1);
                g_buf_multi[g_cursor_multi] = '\n';
                g_cursor_multi++;
                len++;
            }
        }
    }

    font_ensure_text(g_buf_multi);

    // 3. 渲染：手动绘制文本（顶对齐 + 裁剪到框内 + 跟随光标纵向滚动），
    //    彻底避免内容过多时向上溢出、压到上方其它控件。
    Font  font     = GuiGetFont();   // fontSize/spacing/padding/lineH 已在函数顶部声明

    // 先用 GuiTextBox（空串）画出原生边框与底色，文字我们自己画
    char empty[1] = {0};
    GuiTextBox(r, empty, 1, false);

    // 光标所在逻辑行、当前行起点、总行数
    int cursor_line = 0, current_line_start = 0;
    for (int i = 0; i < g_cursor_multi && g_buf_multi[i]; i++) {
        if (g_buf_multi[i] == '\n') { cursor_line++; current_line_start = i + 1; }
    }
    int total_lines = 1;
    for (int i = 0; i < len; i++) if (g_buf_multi[i] == '\n') total_lines++;

    // 可见行数 + 纵向滚动：保证光标行始终在可视区域内
    int visible = (int)((r.height - 2.0f * padding) / lineH);
    if (visible < 1) visible = 1;
    if (cursor_line < g_scroll_multi)             g_scroll_multi = cursor_line;
    if (cursor_line >= g_scroll_multi + visible)  g_scroll_multi = cursor_line - visible + 1;
    int max_scroll = total_lines - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (g_scroll_multi > max_scroll) g_scroll_multi = max_scroll;
    if (g_scroll_multi < 0) g_scroll_multi = 0;

    Color textColor = GetColor(GuiGetStyle(TEXTBOX, editMode ? TEXT_COLOR_FOCUSED : TEXT_COLOR_NORMAL));

    // 裁剪到框内，按 \n 逐行绘制，从滚动起点开始
    BeginScissorMode((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, (int)r.height - 2);
    {
        char tmp[TEXT_BUF_MULTI];
        int line = 0, ls = 0;
        for (int i = 0; i <= len; i++) {
            if (i == len || g_buf_multi[i] == '\n') {
                int seg = i - ls;
                if (seg > 0 && line >= g_scroll_multi && line < g_scroll_multi + visible + 1) {
                    memcpy(tmp, g_buf_multi + ls, seg);
                    tmp[seg] = '\0';
                    float ty = r.y + padding + (line - g_scroll_multi) * lineH;
                    // 选区高亮（落在本行内的部分）
                    if (editMode) {
                        int sa, sb; tbm_sel_range(&sa, &sb);
                        int s0 = sa > ls ? sa : ls;
                        int s1 = sb < i ? sb : i;
                        if (s1 > s0) {
                            char pre[TEXT_BUF_MULTI];
                            int p0 = s0 - ls; memcpy(pre, g_buf_multi + ls, p0); pre[p0] = '\0';
                            float x0 = MeasureTextEx(font, pre, fontSize, spacing).x;
                            int p1 = s1 - ls; memcpy(pre, g_buf_multi + ls, p1); pre[p1] = '\0';
                            float x1 = MeasureTextEx(font, pre, fontSize, spacing).x;
                            Color hl = GetColor(GuiGetStyle(TEXTBOX, BORDER_COLOR_FOCUSED));
                            hl.a = 90;
                            DrawRectangle((int)(r.x + padding + x0), (int)ty, (int)(x1 - x0), (int)lineH, hl);
                        }
                    }
                    DrawTextEx(font, tmp, (Vector2){ r.x + padding, ty }, fontSize, spacing, textColor);
                }
                line++;
                ls = i + 1;
            }
        }

        // 光标（跟随滚动）
        if (editMode) {
            char cur_sub[TEXT_BUF_MULTI];
            int sub_len = g_cursor_multi - current_line_start;
            if (sub_len < 0) sub_len = 0;
            memcpy(cur_sub, g_buf_multi + current_line_start, sub_len);
            cur_sub[sub_len] = '\0';
            Vector2 sz = MeasureTextEx(font, cur_sub, fontSize, spacing);
            float cx = r.x + padding + sz.x + 1;
            float cy = r.y + padding + (cursor_line - g_scroll_multi) * lineH;
            DrawLineV((Vector2){ cx, cy }, (Vector2){ cx, cy + fontSize },
                      GetColor(GuiGetStyle(TEXTBOX, BORDER_COLOR_FOCUSED)));
        }
    }
    EndScissorMode();

    // 聚焦高亮外框
    if (editMode) {
        DrawRectangleLinesEx(r, 1, GetColor(GuiGetStyle(TEXTBOX, BORDER_COLOR_FOCUSED)));
    }

    lua_pushstring(L, g_buf_multi);
    lua_pushboolean(L, editMode);
    lua_pushboolean(L, submitted);
    return 3;
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

    // Color line = GetColor(GuiGetStyle(DEFAULT, LINE_COLOR));
    // Color bg = GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR));
    // float thick = 1.0f;
    // float textSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    // float spacing = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    // float margin = 12.0f;
    // float padding = 6.0f;

    // DrawRectangleRec((Rectangle){ r.x, r.y, thick, r.height }, line);
    // DrawRectangleRec((Rectangle){ r.x, r.y + r.height - thick, r.width, thick }, line);
    // DrawRectangleRec((Rectangle){ r.x + r.width - thick, r.y, thick, r.height }, line);

    // if (text && text[0] != '\0') {
    //     Vector2 textSizePx = MeasureTextEx(GuiGetFont(), text, textSize, spacing);
    //     float textX = r.x + margin;
    //     float textY = r.y - textSize * 0.5f;
    //     float clearX = textX - padding;
    //     float clearW = textSizePx.x + padding * 2.0f;
    //     float leftW = clearX - r.x;
    //     float rightX = clearX + clearW;
    //     float rightW = r.x + r.width - rightX;

    //     DrawRectangleRec((Rectangle){ clearX, textY, clearW, textSizePx.y }, bg);
    //     if (leftW > 0.0f) DrawRectangleRec((Rectangle){ r.x, r.y, leftW, thick }, line);
    //     if (rightW > 0.0f) DrawRectangleRec((Rectangle){ rightX, r.y, rightW, thick }, line);
    //     DrawTextEx(GuiGetFont(), text, (Vector2){ textX, textY }, textSize, spacing, line);
    // } else {
    //     DrawRectangleRec((Rectangle){ r.x, r.y, r.width, thick }, line);
    // }

    return 0;
}

static int l_window(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    const char *text = luaL_checkstring(L,5);
    font_ensure_text(text);
    check_mouse_hover(r);
    // GuiWindowBox = 面板 + 标题栏 + 右上角 ✖ 关闭按钮；点击 ✖ 时返回 true
    int closed = GuiWindowBox(r, text);
    lua_pushboolean(L, closed);
    return 1;
}

// 模态消息/确认框：buttons 用分号分隔，如 "取消;确定"
// 返回：-1=未点击(对话框继续显示)，0=点了右上角 ✖，1=第1个按钮，2=第2个按钮 …
static int l_messagebox(lua_State *L) {
    Rectangle r = {lua_tonumber(L,1),lua_tonumber(L,2),lua_tonumber(L,3),lua_tonumber(L,4)};
    const char *title   = luaL_checkstring(L, 5);
    const char *message = luaL_checkstring(L, 6);
    const char *buttons = luaL_checkstring(L, 7);
    font_ensure_text(title);
    font_ensure_text(message);
    font_ensure_text(buttons);
    check_mouse_hover(r);
    int result = GuiMessageBox(r, title, message, buttons);
    lua_pushinteger(L, result);
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
// 渲染原语（xagent 等需要自绘文本/滚动区域的应用使用）
//============================================================================
static Color rg_color_opt(lua_State *L, int idx) {
    Color c;
    c.r = (unsigned char)luaL_optinteger(L, idx,     255);
    c.g = (unsigned char)luaL_optinteger(L, idx + 1, 255);
    c.b = (unsigned char)luaL_optinteger(L, idx + 2, 255);
    c.a = (unsigned char)luaL_optinteger(L, idx + 3, 255);
    return c;
}

// measure_text(text [, size]) -> width, height
static int l_measure_text(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    float size = (float)luaL_optnumber(L, 2, (lua_Number)GuiGetStyle(DEFAULT, TEXT_SIZE));
    float spacing = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    font_ensure_text(text);
    Vector2 sz = MeasureTextEx(GuiGetFont(), text, size, spacing);
    lua_pushnumber(L, sz.x);
    lua_pushnumber(L, sz.y);
    return 2;
}

// draw_text(text, x, y [, size [, r, g, b, a]])
static int l_draw_text(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float size = (float)luaL_optnumber(L, 4, (lua_Number)GuiGetStyle(DEFAULT, TEXT_SIZE));
    Color c = rg_color_opt(L, 5);
    float spacing = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    font_ensure_text(text);
    DrawTextEx(GuiGetFont(), text, (Vector2){ x, y }, size, spacing, c);
    return 0;
}

// draw_rectangle(x, y, w, h [, r, g, b, a])
static int l_draw_rectangle(lua_State *L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int w = (int)luaL_checknumber(L, 3);
    int h = (int)luaL_checknumber(L, 4);
    DrawRectangle(x, y, w, h, rg_color_opt(L, 5));
    return 0;
}

// begin_scissor(x, y, w, h) / end_scissor() — clip drawing to a rectangle.
static int l_begin_scissor(lua_State *L) {
    BeginScissorMode((int)luaL_checknumber(L, 1), (int)luaL_checknumber(L, 2),
                     (int)luaL_checknumber(L, 3), (int)luaL_checknumber(L, 4));
    return 0;
}
static int l_end_scissor(lua_State *L) { (void)L; EndScissorMode(); return 0; }

// get_wheel() -> dy   (mouse wheel delta this frame)
static int l_get_wheel(lua_State *L) { lua_pushnumber(L, GetMouseWheelMove()); return 1; }

// get_mouse() -> x, y
static int l_get_mouse(lua_State *L) {
    Vector2 m = GetMousePosition();
    lua_pushnumber(L, m.x);
    lua_pushnumber(L, m.y);
    return 2;
}

// screen_size() -> w, h
static int l_screen_size(lua_State *L) {
    lua_pushinteger(L, GetScreenWidth());
    lua_pushinteger(L, GetScreenHeight());
    return 2;
}

// get_style(control, property) -> int  (color props are 0xRRGGBBAA)
static int l_get_style(lua_State *L) {
    int control = (int)luaL_checkinteger(L, 1);
    int property = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, (lua_Integer)(unsigned int)GuiGetStyle(control, property));
    return 1;
}

//============================================================================
// 应用内文件/目录选择对话框（gui_window_file_dialog 封装）
//============================================================================
static GuiWindowFileDialogState g_file_dialog;
static int g_file_dialog_live = 0;

// file_dialog_open(init_path?, w?, h?, dirs_only?) — 打开对话框（居中，默认
// 560x420）。dirs_only=true 时只列目录：过滤串变成 "DIRS*;.__dironly__"，
// 目录靠 DIRS 标签保留，文件因扩展名永不匹配而全部隐藏（选目录场景）。
static int l_file_dialog_open(lua_State *L) {
    const char *path = luaL_optstring(L, 1, NULL);
    int w = (int)luaL_optinteger(L, 2, 560);
    int h = (int)luaL_optinteger(L, 3, 420);
    int dirs_only = lua_toboolean(L, 4);
    g_file_dialog = InitGuiWindowFileDialog(path);
    g_file_dialog.windowBounds = (Rectangle){
        (float)(GetScreenWidth() / 2 - w / 2), (float)(GetScreenHeight() / 2 - h / 2),
        (float)w, (float)h };
    if (dirs_only) strcpy(g_file_dialog.filterExt, ".__dironly__");
    g_file_dialog.windowActive = true;
    g_file_dialog_live = 1;
    return 0;
}

// file_dialog() -> 'active' | 'select', dir, file | 'cancel' | nil(未打开)
// 每帧调用（在主界面之后，叠加绘制）。'select' 时 dir = 当前目录，file = 选中
// 文件名（目录选择场景忽略 file、直接用 dir）。
static int l_file_dialog(lua_State *L) {
    if (!g_file_dialog_live) { lua_pushnil(L); return 1; }
    GuiWindowFileDialog(&g_file_dialog);
    if (g_file_dialog.SelectFilePressed) {
        g_file_dialog.SelectFilePressed = false;
        g_file_dialog_live = 0;
        lua_pushstring(L, "select");
        lua_pushstring(L, g_file_dialog.dirPathText);
        lua_pushstring(L, g_file_dialog.fileNameText);
        return 3;
    }
    if (!g_file_dialog.windowActive) {
        g_file_dialog_live = 0;
        lua_pushstring(L, "cancel");
        return 1;
    }
    lua_pushstring(L, "active");
    return 1;
}

// set_clipboard(text) / get_clipboard() -> text
static int l_set_clipboard(lua_State *L) {
    SetClipboardText(luaL_checkstring(L, 1));
    return 0;
}
static int l_get_clipboard(lua_State *L) {
    const char *s = clipboard_has_text() ? GetClipboardText() : NULL;
    lua_pushstring(L, s ? s : "");
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
    {"messagebox",      l_messagebox},
    {"dropdown",        l_dropdown},
    {"listview",        l_listview},
    {"is_mouse_over_ui",l_is_mouse_over_ui},
    {"set_style",       l_set_style},
    {"load_font",       l_load_font},
    {"load_texture",    l_load_texture},
    {"load_texture_mem", l_load_texture_mem},
    {"unload_texture",  l_unload_texture},
    {"get_clipboard_image", l_get_clipboard_image},
    {"take_pasted_image",   l_take_pasted_image},
    {"file_dialog_open",    l_file_dialog_open},
    {"file_dialog",         l_file_dialog},
    {"draw_texture",    l_draw_texture},
    {"draw_texture_ex", l_draw_texture_ex},
    {"draw_icon",       l_draw_icon},
    {"set_icon_scale",  l_set_icon_scale},
    {"load_style",      l_load_style},
    {"lock",            l_lock},
    {"unlock",          l_unlock},
    {"is_locked",       l_is_locked},
    {"model_view",      l_model_view},
    {"measure_text",    l_measure_text},
    {"draw_text",       l_draw_text},
    {"draw_rectangle",  l_draw_rectangle},
    {"begin_scissor",   l_begin_scissor},
    {"end_scissor",     l_end_scissor},
    {"get_wheel",       l_get_wheel},
    {"get_mouse",       l_get_mouse},
    {"screen_size",     l_screen_size},
    {"set_clipboard",   l_set_clipboard},
    {"get_clipboard",   l_get_clipboard},
    {"get_style",       l_get_style},
    {NULL, NULL}
};

static void register_consts(lua_State *L) {
    // Style constants
    lua_pushinteger(L, 0); lua_setfield(L, -2, "DEFAULT");
    lua_pushinteger(L, 14); lua_setfield(L, -2, "TEXT_ALIGNMENT");
    lua_pushinteger(L, 16); lua_setfield(L, -2, "TEXT_SIZE");
    lua_pushinteger(L, 13); lua_setfield(L, -2, "TEXT_PADDING");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "TEXT_ALIGN_LEFT");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "TEXT_ALIGN_CENTER");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "TEXT_ALIGN_RIGHT");

    // Color & border style properties
    lua_pushinteger(L, 0);  lua_setfield(L, -2, "BORDER_COLOR_NORMAL");
    lua_pushinteger(L, 1);  lua_setfield(L, -2, "BASE_COLOR_NORMAL");
    lua_pushinteger(L, 2);  lua_setfield(L, -2, "TEXT_COLOR_NORMAL");
    lua_pushinteger(L, 3);  lua_setfield(L, -2, "BORDER_COLOR_FOCUSED");
    lua_pushinteger(L, 4);  lua_setfield(L, -2, "BASE_COLOR_FOCUSED");
    lua_pushinteger(L, 5);  lua_setfield(L, -2, "TEXT_COLOR_FOCUSED");
    lua_pushinteger(L, 6);  lua_setfield(L, -2, "BORDER_COLOR_PRESSED");
    lua_pushinteger(L, 7);  lua_setfield(L, -2, "BASE_COLOR_PRESSED");
    lua_pushinteger(L, 8);  lua_setfield(L, -2, "TEXT_COLOR_PRESSED");
    lua_pushinteger(L, 9);  lua_setfield(L, -2, "BORDER_COLOR_DISABLED");
    lua_pushinteger(L, 10); lua_setfield(L, -2, "BASE_COLOR_DISABLED");
    lua_pushinteger(L, 11); lua_setfield(L, -2, "TEXT_COLOR_DISABLED");
    lua_pushinteger(L, 12); lua_setfield(L, -2, "BORDER_WIDTH");
    lua_pushinteger(L, 17); lua_setfield(L, -2, "TEXT_SPACING");
    lua_pushinteger(L, 18); lua_setfield(L, -2, "LINE_COLOR");
    lua_pushinteger(L, 19); lua_setfield(L, -2, "BACKGROUND_COLOR");
    lua_pushinteger(L, 20); lua_setfield(L, -2, "TEXT_LINE_SPACING");

    // Control type IDs
    lua_pushinteger(L, 1); lua_setfield(L, -2, "LABEL");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "BUTTON");
    lua_pushinteger(L, 3); lua_setfield(L, -2, "TOGGLE");
    lua_pushinteger(L, 4); lua_setfield(L, -2, "SLIDER");
    lua_pushinteger(L, 5); lua_setfield(L, -2, "PROGRESSBAR");
    lua_pushinteger(L, 6); lua_setfield(L, -2, "CHECKBOX");
    lua_pushinteger(L, 9); lua_setfield(L, -2, "TEXTBOX");
    lua_pushinteger(L, 10); lua_setfield(L, -2, "VALUEBOX");

    // Icon constants (raygui ricons 4.x)
    lua_pushinteger(L, 0);   lua_setfield(L, -2, "ICON_NONE");
    lua_pushinteger(L, 1);   lua_setfield(L, -2, "ICON_FOLDER_FILE_OPEN");
    lua_pushinteger(L, 2);   lua_setfield(L, -2, "ICON_FILE_SAVE_CLASSIC");
    lua_pushinteger(L, 3);   lua_setfield(L, -2, "ICON_FOLDER_OPEN");
    lua_pushinteger(L, 4);   lua_setfield(L, -2, "ICON_FOLDER_SAVE");
    lua_pushinteger(L, 5);   lua_setfield(L, -2, "ICON_FILE_OPEN");
    lua_pushinteger(L, 6);   lua_setfield(L, -2, "ICON_FILE_SAVE");
    lua_pushinteger(L, 7);   lua_setfield(L, -2, "ICON_FILE_EXPORT");
    lua_pushinteger(L, 8);   lua_setfield(L, -2, "ICON_FILE_ADD");
    lua_pushinteger(L, 9);   lua_setfield(L, -2, "ICON_FILE_DELETE");
    lua_pushinteger(L, 10);  lua_setfield(L, -2, "ICON_FILETYPE_TEXT");
    lua_pushinteger(L, 11);  lua_setfield(L, -2, "ICON_FILETYPE_AUDIO");
    lua_pushinteger(L, 12);  lua_setfield(L, -2, "ICON_FILETYPE_IMAGE");
    lua_pushinteger(L, 13);  lua_setfield(L, -2, "ICON_FILETYPE_PLAY");
    lua_pushinteger(L, 14);  lua_setfield(L, -2, "ICON_FILETYPE_VIDEO");
    lua_pushinteger(L, 15);  lua_setfield(L, -2, "ICON_FILETYPE_INFO");
    lua_pushinteger(L, 16);  lua_setfield(L, -2, "ICON_FILE_COPY");
    lua_pushinteger(L, 17);  lua_setfield(L, -2, "ICON_FILE_CUT");
    lua_pushinteger(L, 18);  lua_setfield(L, -2, "ICON_FILE_PASTE");
    lua_pushinteger(L, 19);  lua_setfield(L, -2, "ICON_CURSOR_HAND");
    lua_pushinteger(L, 20);  lua_setfield(L, -2, "ICON_CURSOR_POINTER");
    lua_pushinteger(L, 21);  lua_setfield(L, -2, "ICON_CURSOR_CLASSIC");
    lua_pushinteger(L, 22);  lua_setfield(L, -2, "ICON_PENCIL");
    lua_pushinteger(L, 23);  lua_setfield(L, -2, "ICON_PENCIL_BIG");
    lua_pushinteger(L, 24);  lua_setfield(L, -2, "ICON_BRUSH_CLASSIC");
    lua_pushinteger(L, 25);  lua_setfield(L, -2, "ICON_BRUSH_PAINTER");
    lua_pushinteger(L, 26);  lua_setfield(L, -2, "ICON_WATER_DROP");
    lua_pushinteger(L, 27);  lua_setfield(L, -2, "ICON_COLOR_PICKER");
    lua_pushinteger(L, 28);  lua_setfield(L, -2, "ICON_RUBBER");
    lua_pushinteger(L, 29);  lua_setfield(L, -2, "ICON_COLOR_BUCKET");
    lua_pushinteger(L, 30);  lua_setfield(L, -2, "ICON_TEXT_T");
    lua_pushinteger(L, 31);  lua_setfield(L, -2, "ICON_TEXT_A");
    lua_pushinteger(L, 32);  lua_setfield(L, -2, "ICON_SCALE");
    lua_pushinteger(L, 33);  lua_setfield(L, -2, "ICON_RESIZE");
    lua_pushinteger(L, 34);  lua_setfield(L, -2, "ICON_FILTER_POINT");
    lua_pushinteger(L, 35);  lua_setfield(L, -2, "ICON_FILTER_BILINEAR");
    lua_pushinteger(L, 36);  lua_setfield(L, -2, "ICON_CROP");
    lua_pushinteger(L, 37);  lua_setfield(L, -2, "ICON_CROP_ALPHA");
    lua_pushinteger(L, 38);  lua_setfield(L, -2, "ICON_SQUARE_TOGGLE");
    lua_pushinteger(L, 39);  lua_setfield(L, -2, "ICON_SYMMETRY");
    lua_pushinteger(L, 40);  lua_setfield(L, -2, "ICON_SYMMETRY_HORIZONTAL");
    lua_pushinteger(L, 41);  lua_setfield(L, -2, "ICON_SYMMETRY_VERTICAL");
    lua_pushinteger(L, 42);  lua_setfield(L, -2, "ICON_LENS");
    lua_pushinteger(L, 43);  lua_setfield(L, -2, "ICON_LENS_BIG");
    lua_pushinteger(L, 44);  lua_setfield(L, -2, "ICON_EYE_ON");
    lua_pushinteger(L, 45);  lua_setfield(L, -2, "ICON_EYE_OFF");
    lua_pushinteger(L, 46);  lua_setfield(L, -2, "ICON_FILTER_TOP");
    lua_pushinteger(L, 47);  lua_setfield(L, -2, "ICON_FILTER");
    lua_pushinteger(L, 48);  lua_setfield(L, -2, "ICON_TARGET_POINT");
    lua_pushinteger(L, 49);  lua_setfield(L, -2, "ICON_TARGET_SMALL");
    lua_pushinteger(L, 50);  lua_setfield(L, -2, "ICON_TARGET_BIG");
    lua_pushinteger(L, 51);  lua_setfield(L, -2, "ICON_TARGET_MOVE");
    lua_pushinteger(L, 52);  lua_setfield(L, -2, "ICON_CURSOR_MOVE");
    lua_pushinteger(L, 53);  lua_setfield(L, -2, "ICON_CURSOR_SCALE");
    lua_pushinteger(L, 54);  lua_setfield(L, -2, "ICON_CURSOR_SCALE_RIGHT");
    lua_pushinteger(L, 55);  lua_setfield(L, -2, "ICON_CURSOR_SCALE_LEFT");
    lua_pushinteger(L, 56);  lua_setfield(L, -2, "ICON_UNDO");
    lua_pushinteger(L, 57);  lua_setfield(L, -2, "ICON_REDO");
    lua_pushinteger(L, 58);  lua_setfield(L, -2, "ICON_REREDO");
    lua_pushinteger(L, 59);  lua_setfield(L, -2, "ICON_MUTATE");
    lua_pushinteger(L, 60);  lua_setfield(L, -2, "ICON_ROTATE");
    lua_pushinteger(L, 61);  lua_setfield(L, -2, "ICON_REPEAT");
    lua_pushinteger(L, 62);  lua_setfield(L, -2, "ICON_SHUFFLE");
    lua_pushinteger(L, 63);  lua_setfield(L, -2, "ICON_EMPTYBOX");
    lua_pushinteger(L, 64);  lua_setfield(L, -2, "ICON_TARGET");
    lua_pushinteger(L, 65);  lua_setfield(L, -2, "ICON_TARGET_SMALL_FILL");
    lua_pushinteger(L, 66);  lua_setfield(L, -2, "ICON_TARGET_BIG_FILL");
    lua_pushinteger(L, 67);  lua_setfield(L, -2, "ICON_TARGET_MOVE_FILL");
    lua_pushinteger(L, 68);  lua_setfield(L, -2, "ICON_CURSOR_MOVE_FILL");
    lua_pushinteger(L, 69);  lua_setfield(L, -2, "ICON_CURSOR_SCALE_FILL");
    lua_pushinteger(L, 70);  lua_setfield(L, -2, "ICON_CURSOR_SCALE_RIGHT_FILL");
    lua_pushinteger(L, 71);  lua_setfield(L, -2, "ICON_CURSOR_SCALE_LEFT_FILL");
    lua_pushinteger(L, 72);  lua_setfield(L, -2, "ICON_UNDO_FILL");
    lua_pushinteger(L, 73);  lua_setfield(L, -2, "ICON_REDO_FILL");
    lua_pushinteger(L, 74);  lua_setfield(L, -2, "ICON_REREDO_FILL");
    lua_pushinteger(L, 75);  lua_setfield(L, -2, "ICON_MUTATE_FILL");
    lua_pushinteger(L, 76);  lua_setfield(L, -2, "ICON_ROTATE_FILL");
    lua_pushinteger(L, 77);  lua_setfield(L, -2, "ICON_REPEAT_FILL");
    lua_pushinteger(L, 78);  lua_setfield(L, -2, "ICON_SHUFFLE_FILL");
    lua_pushinteger(L, 79);  lua_setfield(L, -2, "ICON_EMPTYBOX_SMALL");
    lua_pushinteger(L, 80);  lua_setfield(L, -2, "ICON_BOX");
    lua_pushinteger(L, 81);  lua_setfield(L, -2, "ICON_BOX_TOP");
    lua_pushinteger(L, 82);  lua_setfield(L, -2, "ICON_BOX_TOP_RIGHT");
    lua_pushinteger(L, 83);  lua_setfield(L, -2, "ICON_BOX_RIGHT");
    lua_pushinteger(L, 84);  lua_setfield(L, -2, "ICON_BOX_BOTTOM_RIGHT");
    lua_pushinteger(L, 85);  lua_setfield(L, -2, "ICON_BOX_BOTTOM");
    lua_pushinteger(L, 86);  lua_setfield(L, -2, "ICON_BOX_BOTTOM_LEFT");
    lua_pushinteger(L, 87);  lua_setfield(L, -2, "ICON_BOX_LEFT");
    lua_pushinteger(L, 88);  lua_setfield(L, -2, "ICON_BOX_TOP_LEFT");
    lua_pushinteger(L, 89);  lua_setfield(L, -2, "ICON_BOX_CENTER");
    lua_pushinteger(L, 90);  lua_setfield(L, -2, "ICON_BOX_CIRCLE_MASK");
    lua_pushinteger(L, 91);  lua_setfield(L, -2, "ICON_POT");
    lua_pushinteger(L, 92);  lua_setfield(L, -2, "ICON_ALPHA_MULTIPLY");
    lua_pushinteger(L, 93);  lua_setfield(L, -2, "ICON_ALPHA_CLEAR");
    lua_pushinteger(L, 94);  lua_setfield(L, -2, "ICON_DITHERING");
    lua_pushinteger(L, 95);  lua_setfield(L, -2, "ICON_MIPMAPS");
    lua_pushinteger(L, 96);  lua_setfield(L, -2, "ICON_BOX_GRID");
    lua_pushinteger(L, 97);  lua_setfield(L, -2, "ICON_GRID");
    lua_pushinteger(L, 98);  lua_setfield(L, -2, "ICON_BOX_CORNERS_SMALL");
    lua_pushinteger(L, 99);  lua_setfield(L, -2, "ICON_BOX_CORNERS_BIG");
    lua_pushinteger(L, 100); lua_setfield(L, -2, "ICON_FOUR_BOXES");
    lua_pushinteger(L, 101); lua_setfield(L, -2, "ICON_GRID_FILL");
    lua_pushinteger(L, 102); lua_setfield(L, -2, "ICON_BOX_MULTISIZE");
    lua_pushinteger(L, 103); lua_setfield(L, -2, "ICON_ZOOM_SMALL");
    lua_pushinteger(L, 104); lua_setfield(L, -2, "ICON_ZOOM_MEDIUM");
    lua_pushinteger(L, 105); lua_setfield(L, -2, "ICON_ZOOM_BIG");
    lua_pushinteger(L, 106); lua_setfield(L, -2, "ICON_ZOOM_ALL");
    lua_pushinteger(L, 107); lua_setfield(L, -2, "ICON_ZOOM_CENTER");
    lua_pushinteger(L, 108); lua_setfield(L, -2, "ICON_BOX_DOTS_SMALL");
    lua_pushinteger(L, 109); lua_setfield(L, -2, "ICON_BOX_DOTS_BIG");
    lua_pushinteger(L, 110); lua_setfield(L, -2, "ICON_BOX_CONCENTRIC");
    lua_pushinteger(L, 111); lua_setfield(L, -2, "ICON_BOX_GRID_BIG");
    lua_pushinteger(L, 112); lua_setfield(L, -2, "ICON_OK_TICK");
    lua_pushinteger(L, 113); lua_setfield(L, -2, "ICON_CROSS");
    lua_pushinteger(L, 114); lua_setfield(L, -2, "ICON_ARROW_LEFT");
    lua_pushinteger(L, 115); lua_setfield(L, -2, "ICON_ARROW_RIGHT");
    lua_pushinteger(L, 116); lua_setfield(L, -2, "ICON_ARROW_DOWN");
    lua_pushinteger(L, 117); lua_setfield(L, -2, "ICON_ARROW_UP");
    lua_pushinteger(L, 118); lua_setfield(L, -2, "ICON_ARROW_LEFT_FILL");
    lua_pushinteger(L, 119); lua_setfield(L, -2, "ICON_ARROW_RIGHT_FILL");
    lua_pushinteger(L, 120); lua_setfield(L, -2, "ICON_ARROW_DOWN_FILL");
    lua_pushinteger(L, 121); lua_setfield(L, -2, "ICON_ARROW_UP_FILL");
    lua_pushinteger(L, 122); lua_setfield(L, -2, "ICON_AUDIO");
    lua_pushinteger(L, 123); lua_setfield(L, -2, "ICON_FX");
    lua_pushinteger(L, 124); lua_setfield(L, -2, "ICON_WAVE");
    lua_pushinteger(L, 125); lua_setfield(L, -2, "ICON_WAVE_SINUS");
    lua_pushinteger(L, 126); lua_setfield(L, -2, "ICON_WAVE_SQUARE");
    lua_pushinteger(L, 127); lua_setfield(L, -2, "ICON_WAVE_TRIANGULAR");
    lua_pushinteger(L, 128); lua_setfield(L, -2, "ICON_CROSS_SMALL");
    lua_pushinteger(L, 129); lua_setfield(L, -2, "ICON_PLAYER_PREVIOUS");
    lua_pushinteger(L, 130); lua_setfield(L, -2, "ICON_PLAYER_PLAY_BACK");
    lua_pushinteger(L, 131); lua_setfield(L, -2, "ICON_PLAYER_PLAY");
    lua_pushinteger(L, 132); lua_setfield(L, -2, "ICON_PLAYER_PAUSE");
    lua_pushinteger(L, 133); lua_setfield(L, -2, "ICON_PLAYER_STOP");
    lua_pushinteger(L, 134); lua_setfield(L, -2, "ICON_PLAYER_NEXT");
    lua_pushinteger(L, 135); lua_setfield(L, -2, "ICON_PLAYER_RECORD");
    lua_pushinteger(L, 136); lua_setfield(L, -2, "ICON_MAGNET");
    lua_pushinteger(L, 137); lua_setfield(L, -2, "ICON_LOCK_CLOSE");
    lua_pushinteger(L, 138); lua_setfield(L, -2, "ICON_LOCK_OPEN");
    lua_pushinteger(L, 139); lua_setfield(L, -2, "ICON_CLOCK");
    lua_pushinteger(L, 140); lua_setfield(L, -2, "ICON_TOOLS");
    lua_pushinteger(L, 141); lua_setfield(L, -2, "ICON_GEAR");
    lua_pushinteger(L, 142); lua_setfield(L, -2, "ICON_GEAR_BIG");
    lua_pushinteger(L, 143); lua_setfield(L, -2, "ICON_BIN");
    lua_pushinteger(L, 144); lua_setfield(L, -2, "ICON_HAND_POINTER");
    lua_pushinteger(L, 145); lua_setfield(L, -2, "ICON_LASER");
    lua_pushinteger(L, 146); lua_setfield(L, -2, "ICON_COIN");
    lua_pushinteger(L, 147); lua_setfield(L, -2, "ICON_EXPLOSION");
    lua_pushinteger(L, 148); lua_setfield(L, -2, "ICON_1UP");
    lua_pushinteger(L, 149); lua_setfield(L, -2, "ICON_PLAYER");
    lua_pushinteger(L, 150); lua_setfield(L, -2, "ICON_PLAYER_JUMP");
    lua_pushinteger(L, 151); lua_setfield(L, -2, "ICON_KEY");
    lua_pushinteger(L, 152); lua_setfield(L, -2, "ICON_DEMON");
    lua_pushinteger(L, 153); lua_setfield(L, -2, "ICON_TEXT_POPUP");
    lua_pushinteger(L, 154); lua_setfield(L, -2, "ICON_GEAR_EX");
    lua_pushinteger(L, 155); lua_setfield(L, -2, "ICON_CRACK");
    lua_pushinteger(L, 156); lua_setfield(L, -2, "ICON_CRACK_POINTS");
    lua_pushinteger(L, 157); lua_setfield(L, -2, "ICON_STAR");
    lua_pushinteger(L, 158); lua_setfield(L, -2, "ICON_DOOR");
    lua_pushinteger(L, 159); lua_setfield(L, -2, "ICON_EXIT");
    lua_pushinteger(L, 160); lua_setfield(L, -2, "ICON_MODE_2D");
    lua_pushinteger(L, 161); lua_setfield(L, -2, "ICON_MODE_3D");
    lua_pushinteger(L, 162); lua_setfield(L, -2, "ICON_CUBE");
    lua_pushinteger(L, 163); lua_setfield(L, -2, "ICON_CUBE_FACE_TOP");
    lua_pushinteger(L, 164); lua_setfield(L, -2, "ICON_CUBE_FACE_LEFT");
    lua_pushinteger(L, 165); lua_setfield(L, -2, "ICON_CUBE_FACE_FRONT");
    lua_pushinteger(L, 166); lua_setfield(L, -2, "ICON_CUBE_FACE_BOTTOM");
    lua_pushinteger(L, 167); lua_setfield(L, -2, "ICON_CUBE_FACE_RIGHT");
    lua_pushinteger(L, 168); lua_setfield(L, -2, "ICON_CUBE_FACE_BACK");
    lua_pushinteger(L, 169); lua_setfield(L, -2, "ICON_CAMERA");
    lua_pushinteger(L, 170); lua_setfield(L, -2, "ICON_SPECIAL");
    lua_pushinteger(L, 171); lua_setfield(L, -2, "ICON_LINK_NET");
    lua_pushinteger(L, 172); lua_setfield(L, -2, "ICON_LINK_BOXES");
    lua_pushinteger(L, 173); lua_setfield(L, -2, "ICON_LINK_MULTI");
    lua_pushinteger(L, 174); lua_setfield(L, -2, "ICON_LINK");
    lua_pushinteger(L, 175); lua_setfield(L, -2, "ICON_LINK_BROKE");
    lua_pushinteger(L, 176); lua_setfield(L, -2, "ICON_TEXT_NOTES");
    lua_pushinteger(L, 177); lua_setfield(L, -2, "ICON_NOTEBOOK");
    lua_pushinteger(L, 178); lua_setfield(L, -2, "ICON_SUITCASE");
    lua_pushinteger(L, 179); lua_setfield(L, -2, "ICON_SUITCASE_ZIP");
    lua_pushinteger(L, 180); lua_setfield(L, -2, "ICON_MAILBOX");
    lua_pushinteger(L, 181); lua_setfield(L, -2, "ICON_MONITOR");
    lua_pushinteger(L, 182); lua_setfield(L, -2, "ICON_PRINTER");
    lua_pushinteger(L, 183); lua_setfield(L, -2, "ICON_PHOTO_CAMERA");
    lua_pushinteger(L, 184); lua_setfield(L, -2, "ICON_PHOTO_CAMERA_FLASH");
    lua_pushinteger(L, 185); lua_setfield(L, -2, "ICON_HOUSE");
    lua_pushinteger(L, 186); lua_setfield(L, -2, "ICON_HEART");
    lua_pushinteger(L, 187); lua_setfield(L, -2, "ICON_CORNER");
    lua_pushinteger(L, 188); lua_setfield(L, -2, "ICON_VERTICAL_BARS");
    lua_pushinteger(L, 189); lua_setfield(L, -2, "ICON_VERTICAL_BARS_FILL");
    lua_pushinteger(L, 190); lua_setfield(L, -2, "ICON_LIFE_BARS");
    lua_pushinteger(L, 191); lua_setfield(L, -2, "ICON_INFO");
    lua_pushinteger(L, 192); lua_setfield(L, -2, "ICON_CROSSLINE");
    lua_pushinteger(L, 193); lua_setfield(L, -2, "ICON_HELP");
    lua_pushinteger(L, 194); lua_setfield(L, -2, "ICON_FILETYPE_ALPHA");
    lua_pushinteger(L, 195); lua_setfield(L, -2, "ICON_FILETYPE_HOME");
    lua_pushinteger(L, 196); lua_setfield(L, -2, "ICON_LAYERS_VISIBLE");
    lua_pushinteger(L, 197); lua_setfield(L, -2, "ICON_LAYERS");
    lua_pushinteger(L, 198); lua_setfield(L, -2, "ICON_WINDOW");
    lua_pushinteger(L, 199); lua_setfield(L, -2, "ICON_HIDPI");
    lua_pushinteger(L, 200); lua_setfield(L, -2, "ICON_FILETYPE_BINARY");
    lua_pushinteger(L, 201); lua_setfield(L, -2, "ICON_HEX");
    lua_pushinteger(L, 202); lua_setfield(L, -2, "ICON_SHIELD");
    lua_pushinteger(L, 203); lua_setfield(L, -2, "ICON_FILE_NEW");
    lua_pushinteger(L, 204); lua_setfield(L, -2, "ICON_FOLDER_ADD");
    lua_pushinteger(L, 205); lua_setfield(L, -2, "ICON_ALARM");
    lua_pushinteger(L, 206); lua_setfield(L, -2, "ICON_CPU");
    lua_pushinteger(L, 207); lua_setfield(L, -2, "ICON_ROM");
    lua_pushinteger(L, 208); lua_setfield(L, -2, "ICON_STEP_OVER");
    lua_pushinteger(L, 209); lua_setfield(L, -2, "ICON_STEP_INTO");
    lua_pushinteger(L, 210); lua_setfield(L, -2, "ICON_STEP_OUT");
    lua_pushinteger(L, 211); lua_setfield(L, -2, "ICON_RESTART");
    lua_pushinteger(L, 212); lua_setfield(L, -2, "ICON_BREAKPOINT_ON");
    lua_pushinteger(L, 213); lua_setfield(L, -2, "ICON_BREAKPOINT_OFF");
    lua_pushinteger(L, 214); lua_setfield(L, -2, "ICON_BURGER_MENU");
    lua_pushinteger(L, 215); lua_setfield(L, -2, "ICON_CASE_SENSITIVE");
    lua_pushinteger(L, 216); lua_setfield(L, -2, "ICON_REG_EXP");
    lua_pushinteger(L, 217); lua_setfield(L, -2, "ICON_FOLDER");
    lua_pushinteger(L, 218); lua_setfield(L, -2, "ICON_FILE");
    lua_pushinteger(L, 219); lua_setfield(L, -2, "ICON_SAND_TIMER");
    lua_pushinteger(L, 220); lua_setfield(L, -2, "ICON_WARNING");
    lua_pushinteger(L, 221); lua_setfield(L, -2, "ICON_HELP_BOX");
    lua_pushinteger(L, 222); lua_setfield(L, -2, "ICON_INFO_BOX");
    lua_pushinteger(L, 223); lua_setfield(L, -2, "ICON_PRIORITY");
    lua_pushinteger(L, 224); lua_setfield(L, -2, "ICON_LAYERS_ISO");
    lua_pushinteger(L, 225); lua_setfield(L, -2, "ICON_LAYERS2");
    lua_pushinteger(L, 226); lua_setfield(L, -2, "ICON_MLAYERS");
    lua_pushinteger(L, 227); lua_setfield(L, -2, "ICON_MAPS");
    lua_pushinteger(L, 228); lua_setfield(L, -2, "ICON_HOT");
    lua_pushinteger(L, 229); lua_setfield(L, -2, "ICON_LABEL");
    lua_pushinteger(L, 230); lua_setfield(L, -2, "ICON_NAME_ID");
    lua_pushinteger(L, 231); lua_setfield(L, -2, "ICON_SLICING");
    lua_pushinteger(L, 232); lua_setfield(L, -2, "ICON_MANUAL_CONTROL");
    lua_pushinteger(L, 233); lua_setfield(L, -2, "ICON_COLLISION");
    lua_pushinteger(L, 234); lua_setfield(L, -2, "ICON_CIRCLE_ADD");
    lua_pushinteger(L, 235); lua_setfield(L, -2, "ICON_CIRCLE_ADD_FILL");
    lua_pushinteger(L, 236); lua_setfield(L, -2, "ICON_CIRCLE_WARNING");
    lua_pushinteger(L, 237); lua_setfield(L, -2, "ICON_CIRCLE_WARNING_FILL");
    lua_pushinteger(L, 238); lua_setfield(L, -2, "ICON_BOX_MORE");
    lua_pushinteger(L, 239); lua_setfield(L, -2, "ICON_BOX_MORE_FILL");
    lua_pushinteger(L, 240); lua_setfield(L, -2, "ICON_BOX_MINUS");
    lua_pushinteger(L, 241); lua_setfield(L, -2, "ICON_BOX_MINUS_FILL");
    lua_pushinteger(L, 242); lua_setfield(L, -2, "ICON_UNION");
    lua_pushinteger(L, 243); lua_setfield(L, -2, "ICON_INTERSECTION");
    lua_pushinteger(L, 244); lua_setfield(L, -2, "ICON_DIFFERENCE");
    lua_pushinteger(L, 245); lua_setfield(L, -2, "ICON_SPHERE");
    lua_pushinteger(L, 246); lua_setfield(L, -2, "ICON_CYLINDER");
    lua_pushinteger(L, 247); lua_setfield(L, -2, "ICON_CONE");
    lua_pushinteger(L, 248); lua_setfield(L, -2, "ICON_ELLIPSOID");
    lua_pushinteger(L, 249); lua_setfield(L, -2, "ICON_CAPSULE");
}

EXPORT int luaopen_raygui(lua_State *L) {
    luaL_newlib(L, raygui_lib);
    register_consts(L);
    return 1;
}
