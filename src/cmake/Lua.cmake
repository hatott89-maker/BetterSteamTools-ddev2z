# Defines a static `lua_static` target by fetching the official Lua 5.5
# source release. Idempotent.
if(TARGET lua_static)
    return()
endif()

include(FetchCache)
include(FetchContent)

FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.5.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(lua)

# Support both official tarball layout (with src/) and git repository layout (flat).
if(EXISTS "${lua_SOURCE_DIR}/src/lua.h")
    set(LUA_SOURCE_ROOT "${lua_SOURCE_DIR}/src")
else()
    set(LUA_SOURCE_ROOT "${lua_SOURCE_DIR}")
endif()

if(NOT EXISTS "${LUA_SOURCE_ROOT}/lua.hpp")
    file(WRITE "${LUA_SOURCE_ROOT}/lua.hpp" "// lua.hpp\nextern \"C\" {\n#include \"lua.h\"\n#include \"lualib.h\"\n#include \"lauxlib.h\"\n}\n")
endif()

file(GLOB LUA_CORE_SOURCES "${LUA_SOURCE_ROOT}/*.c")
list(REMOVE_ITEM LUA_CORE_SOURCES
    "${LUA_SOURCE_ROOT}/lua.c"
    "${LUA_SOURCE_ROOT}/luac.c")

add_library(lua_static STATIC ${LUA_CORE_SOURCES})
target_include_directories(lua_static PUBLIC "${LUA_SOURCE_ROOT}")
set_target_properties(lua_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
