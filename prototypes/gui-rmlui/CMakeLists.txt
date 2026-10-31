cmake_minimum_required(VERSION 3.24)
project(sint_gui_prototype C CXX)

# Прототип GUI (D-014): RmlUi + pugl внутри CLAP-плагина.
# Зависимости закреплены по версии и контрольной сумме, системный FreeType не нужен.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT WIN32)
  add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/glibc_compat.h)
endif()

include(FetchContent)

FetchContent_Declare(freetype
  URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
  URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_Declare(rmlui
  URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
  URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_Declare(pugl
  URL https://github.com/lv2/pugl/archive/88b4a59140ff6d7ea74c90223538937f9ab6f9a5.tar.gz
  URL_HASH SHA256=ea48809b033ce35150f955a759fcc285e915619f5561a4d935373db757c435f3
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)

# FreeType: только статическая библиотека, без внешних зависимостей
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)
add_library(Freetype::Freetype ALIAS freetype)
set(FREETYPE_FOUND TRUE)
set(Freetype_FOUND TRUE)

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rmlui)

# pugl использует meson, поэтому собираем его исходники напрямую
FetchContent_MakeAvailable(pugl)
set(PUGL_SRC ${pugl_SOURCE_DIR}/src)
if(WIN32)
  set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/win.c ${PUGL_SRC}/win_gl.c)
else()
  set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/x11.c ${PUGL_SRC}/x11_gl.c)
endif()
add_library(pugl_static STATIC ${PUGL_SRC}/common.c ${PUGL_SRC}/internal.c ${PUGL_PLATFORM_SOURCES})
target_include_directories(pugl_static PUBLIC ${pugl_SOURCE_DIR}/include)
target_compile_definitions(pugl_static PUBLIC PUGL_STATIC PUGL_API= PRIVATE PUGL_INTERNAL)
if(WIN32)
  target_compile_definitions(pugl_static PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(pugl_static PUBLIC opengl32 gdi32 user32 dwmapi)
else()
  # Необязательные расширения X11 выключены: меньше зависимостей у готового плагина
  target_compile_definitions(pugl_static PRIVATE USE_XCURSOR=0 USE_XRANDR=0 USE_XSYNC=0 _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  find_package(X11 REQUIRED)
  find_package(OpenGL REQUIRED)
  target_link_libraries(pugl_static PUBLIC X11::X11 OpenGL::GL OpenGL::GLX)
endif()

# Шрифт встраивается в бинарник: плагин остаётся одним файлом
set(FONT_FILE ${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf)
file(READ ${FONT_FILE} FONT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/embedded_font.cpp
  "extern const unsigned char embeddedFont[] = {${FONT_BYTES}};\n"
  "extern const unsigned int embeddedFontSize = sizeof(embeddedFont);\n")

add_library(sint_gui_prototype MODULE
  plugin.cpp
  ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/embedded_font.cpp
)
target_include_directories(sint_gui_prototype PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${rmlui_SOURCE_DIR}/Backends
)
target_link_libraries(sint_gui_prototype PRIVATE RmlUi::Core pugl_static ${CMAKE_DL_LIBS})
set_target_properties(sint_gui_prototype PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "sint-gui-prototype")
if(NOT WIN32)
  target_link_options(sint_gui_prototype PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)

  add_executable(test_host test_host.cpp)
  target_include_directories(test_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  target_link_libraries(test_host PRIVATE X11::X11 ${CMAKE_DL_LIBS})
endif()
