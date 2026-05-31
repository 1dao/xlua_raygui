# ============================================================================
# 自动检测操作系统平台
# ============================================================================
ifeq ($(OS),Windows_NT)
    PLATFORM = WINDOWS
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        PLATFORM = LINUX
    else
        PLATFORM = UNSUPPORTED
    endif
endif

# ============================================================================
# 通用编译配置
# ============================================================================
CC = gcc
CFLAGS = -shared -fPIC -O2 -DRAYGUI_IMPLEMENTATION
INCLUDES = -I../raylib/src -I../raygui/src

# ============================================================================
# 针对不同操作系统进行差异化配置
# ============================================================================
ifeq ($(PLATFORM),WINDOWS)
    TARGET = raygui.dll
    
    # ✨【关键补充】：Windows 平台下运行环境的 Lua 动态库核心。
    # 默认以标准 Lua 5.5 (lua54.dll) 为例。如果你使用的是 LuaJIT，请将其修改为 luajit-51.dll
    LUA_LIB = lua55.dll
    
    # Windows 依赖项：静态链接 raylib，动态链接 Lua 核心，以及 Win32 原生图形/多媒体系统库
    LIBS = ../raylib/src/libraylib.a $(LUA_LIB) -lgdi32 -luser32 -lm -lwinmm

else ifeq ($(PLATFORM),LINUX)
    TARGET = raygui.so
    
    # ✨【关键补充】：Linux 平台下系统自带或通过包管理器安装的 Lua 动态链接标志。
    # 默认以 Linux 的 Lua 5.4 开发包为例 (-llua5.5)。如果是标准 Lua5.1/LuaJIT 可改为 -llua5.1 或 -llua
    LUA_LIB_FLAG = -llua5.5
    
    # Linux 依赖项：除了 raylib 静态库和 Lua，Linux 还需要显式链接 OpenGL(GL)、X11窗口、线程及系统运行时
    LIBS = ../raylib/src/libraylib.a $(LUA_LIB_FLAG) -lGL -lm -lpthread -ldl -lrt -lX11

else
    $(error 抱歉，当前仅支持 Windows (MinGW) 和 Linux 编译环境！)
endif

# ============================================================================
# 编译规则
# ============================================================================
all: $(TARGET)

$(TARGET): lua_raygui.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ lua_raygui.c $(LIBS)

# 清理编译产物
clean:
	rm -f raygui.dll raygui.so