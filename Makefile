# ============================================================================
# 自动检测操作系统平台
# ============================================================================
ifeq ($(OS),Windows_NT)
    PLATFORM = WINDOWS
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        PLATFORM = LINUX
    else ifeq ($(UNAME_S),Darwin)
        PLATFORM = MACOS
    else
        PLATFORM = UNSUPPORTED
    endif
endif

# ============================================================================
# 通用编译选项（编译期；链接方式/系统库放到各平台分支里）
# ============================================================================
CC       = gcc
CFLAGS   = -fPIC -Os -ffunction-sections -fdata-sections -fvisibility=hidden -Wall -Wextra
INCLUDES = -I../raylib/src -I../raygui/src

# ============================================================================
# 针对不同操作系统进行差异化配置
# ============================================================================
ifeq ($(PLATFORM),WINDOWS)
    TARGET  = raygui.dll
    SHARED  = -shared
    LDFLAGS = -Wl,--gc-sections,-s

    # Windows 下运行环境的 Lua 动态库（标准 Lua 5.5 = lua55.dll；LuaJIT 改成 luajit-51.dll）
    LUA_LIB = lua55.dll
    # 静态链接 raylib + 动态链接 Lua + Win32 图形/多媒体系统库
    LIBS    = ../raylib/src/libraylib.a $(LUA_LIB) -lgdi32 -luser32 -lm -lwinmm

else ifeq ($(PLATFORM),LINUX)
    TARGET  = raygui.so
    SHARED  = -shared
    LDFLAGS = -Wl,--gc-sections,-s

    # Lua 开发包链接标志（按发行版可能是 -llua5.4 / -llua / -lluajit-5.1）
    LUA_LIB_FLAG = -llua5.5
    # 静态链接 raylib 时，内置 GLFW 还会引用这些 X11 扩展库，
    # 缺了会报 undefined reference（XRRGetScreenResources / XineramaQueryScreens 等）。
    LIBS    = ../raylib/src/libraylib.a $(LUA_LIB_FLAG) \
              -lGL -lm -lpthread -ldl -lrt \
              -lX11 -lXrandr -lXinerama -lXi -lXcursor

else ifeq ($(PLATFORM),MACOS)
    CC      = clang
    TARGET  = raygui.so
    # macOS 的 Lua C 模块用 bundle：lua_* 符号在加载时由宿主(lua 可执行文件)解析，
    # 因此无需在此链接 Lua 库。
    SHARED  = -bundle -undefined dynamic_lookup
    # Apple ld64 不认识 --gc-sections / -s，等价做法是 -dead_strip。
    LDFLAGS = -Wl,-dead_strip

    # raylib 在 macOS 依赖的系统框架（raygui 无音频，故不需要 CoreAudio/AudioToolbox）
    LIBS    = ../raylib/src/libraylib.a \
              -framework CoreVideo -framework IOKit -framework Cocoa -framework OpenGL

else
    $(error 抱歉，当前仅支持 Windows (MinGW) / Linux / macOS 编译环境！)
endif

# ============================================================================
# 编译规则
# ============================================================================
all: $(TARGET)

$(TARGET): lua_raygui.c
	$(CC) $(SHARED) $(CFLAGS) $(INCLUDES) -o $@ lua_raygui.c $(LIBS) $(LDFLAGS)

# 清理编译产物
clean:
	rm -f raygui.dll raygui.so
