// Native regression probe. Run from xlua_raygui with raygui.dll and lua55.dll
// on PATH; focus this test window and keep the pointer inside it.
#include <windows.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static void run(lua_State *L, const char *code) {
    if (luaL_dostring(L, code) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        ExitProcess(1);
    }
}

static int owns_pointer(HWND window) {
    CURSORINFO ci = { .cbSize = sizeof(ci) };
    return GetForegroundWindow() == window && GetCursorInfo(&ci)
        && WindowFromPoint(ci.ptScreenPos) == window;
}

static void frame(lua_State *L) {
    run(L, "r.begin(); r.draw_text('Cursor recovery test', 30, 60, 20, 30, 30, 30, 255); r.finish()");
}

static int recover(lua_State *L, int shape, int id) {
    char code[80];
    snprintf(code, sizeof(code), "r.set_mouse_cursor(%d)", shape);
    run(L, code);
    SetCursor(NULL); // Simulate native cursor loss without changing raylib's mode.
    CURSORINFO before = { .cbSize = sizeof(before) };
    GetCursorInfo(&before);
    if (before.hCursor) return 0;
    frame(L);
    CURSORINFO after = { .cbSize = sizeof(after) };
    GetCursorInfo(&after);
    return (after.flags & CURSOR_SHOWING) &&
        after.hCursor == LoadCursorW(NULL, MAKEINTRESOURCEW(id));
}

int main(void) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    run(L, "package.cpath='./?.dll;'..package.cpath; r=require('raygui'); r.init(640,400,'cursor-recovery',{undecorated=true})");
    HWND window = FindWindowW(L"GLFW30", L"cursor-recovery");
    int failed = 0, ready = 0;
    for (int i = 0; i < 3600; i++) {
        frame(L);
        if (owns_pointer(window)) { ready = 1; break; }
    }
    if (!ready) { fprintf(stderr, "FAIL: test window never received the pointer\n"); failed++; }
    if (ready) {
        const char *states[] = { "windowed", "maximized", "restored" };
        for (int state = 0; state < 3; state++) {
            if (state == 1) run(L, "r.maximize_window()");
            if (state == 2) run(L, "r.restore_window()");
            for (int i = 0; i < 10; i++) frame(L);
            int ok = owns_pointer(window) && recover(L, 0, 32512) && recover(L, 5, 32644);
            printf("%s: default and resize cursor recovery %s\n", states[state], ok ? "PASS" : "FAIL");
            if (!ok) failed++;
        }
        // A valid native pointer belongs to the current UI state; leave it alone.
        HCURSOR cross = LoadCursorW(NULL, MAKEINTRESOURCEW(32515));
        SetCursor(cross);
        frame(L);
        CURSORINFO ci = { .cbSize = sizeof(ci) };
        GetCursorInfo(&ci);
        int ok = ci.hCursor == cross;
        printf("valid native cursor preserved %s\n", ok ? "PASS" : "FAIL");
        if (!ok) failed++;
    }
    run(L, "r.close()");
    lua_close(L);
    return failed ? 1 : 0;
}
