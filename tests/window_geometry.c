// Build from xlua_raygui: gcc -o window_geometry.exe tests/window_geometry.c lua55.dll -luser32
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

static int check(lua_State *L, HWND window, const char *phase) {
    RECT rect;
    GetClientRect(window, &rect);
    run(L, "return r.screen_size()");
    int w = (int)lua_tointeger(L, -2), h = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int ok = w == rect.right - rect.left && h == rect.bottom - rect.top;
    printf("%s: layout=%dx%d client=%ldx%ld %s\n", phase, w, h,
        rect.right - rect.left, rect.bottom - rect.top, ok ? "PASS" : "FAIL");
    return ok;
}

int main(void) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    run(L, "package.cpath='./?.dll;'..package.cpath; r=require('raygui'); r.init(960,700,'window-geometry',{undecorated=true,desktop_maximize=true})");
    HWND window = FindWindowW(L"GLFW30", L"window-geometry");
    run(L, "r.maximize_window(); for i=1,5 do r.begin(); r.finish() end");
    int ok = check(L, window, "maximized");
    RECT before, after;
    GetClientRect(window, &before);
    // A stale resize gesture must not corrupt the layout size while maximized.
    run(L, "r.set_window_size(960,700)");
    GetClientRect(window, &after);
    int unchanged = IsZoomed(window) && EqualRect(&before, &after);
    printf("maximized bounds preserved %s\n", unchanged ? "PASS" : "FAIL");
    ok = unchanged && ok;
    ok = check(L, window, "resize while maximized") && ok;
    run(L, "r.restore_window(); for i=1,5 do r.begin(); r.finish() end");
    ok = check(L, window, "restored") && ok;
    run(L, "r.set_window_size(800,600); for i=1,5 do r.begin(); r.finish() end");
    ok = check(L, window, "normal resize") && ok;
    run(L, "r.close()");
    lua_close(L);
    return ok ? 0 : 1;
}
