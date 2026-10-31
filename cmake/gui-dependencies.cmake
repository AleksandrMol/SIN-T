# Зависимости редактора (D-014): всё статически, плагин остаётся одним файлом (D-016).
# Версии и контрольные суммы закреплены, системный FreeType не нужен.

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Плагин должен загружаться в среде со старой glibc (D-019)
function(sint_apply_glibc_compat target)
  if(NOT WIN32)
    target_compile_options(${target} PRIVATE -include ${PROJECT_SOURCE_DIR}/tools/glibcCompat.h)
  endif()
endfunction()

FetchContent_Declare(freetype
  URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
  URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  EXCLUDE_FROM_ALL
)
FetchContent_Declare(rmlui
  URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
  URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  EXCLUDE_FROM_ALL
)
FetchContent_Declare(pugl
  URL https://github.com/lv2/pugl/archive/88b4a59140ff6d7ea74c90223538937f9ab6f9a5.tar.gz
  URL_HASH SHA256=ea48809b033ce35150f955a759fcc285e915619f5561a4d935373db757c435f3
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  EXCLUDE_FROM_ALL
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

# pugl использует meson, поэтому его исходники собираются напрямую
FetchContent_MakeAvailable(pugl)
set(PUGL_SRC ${pugl_SOURCE_DIR}/src)
if(WIN32)
  set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/win.c ${PUGL_SRC}/win_gl.c)
else()
  set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/x11.c ${PUGL_SRC}/x11_gl.c)
endif()
add_library(pugl_static STATIC ${PUGL_SRC}/common.c ${PUGL_SRC}/internal.c ${PUGL_PLATFORM_SOURCES})
target_include_directories(pugl_static SYSTEM PUBLIC ${pugl_SOURCE_DIR}/include)
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

# Без отладочной информации чужого кода плагин весит в разы меньше
foreach(target freetype rmlui_core pugl_static)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -g0)
  endif()
  sint_apply_glibc_compat(${target})
endforeach()

# Шрифт с кириллицей встраивается в бинарник: имена пресетов могут быть русскими
set(SINT_FONT_FILE ${CMAKE_BINARY_DIR}/NotoSans-Regular.ttf)
file(DOWNLOAD
  https://raw.githubusercontent.com/notofonts/notofonts.github.io/28b15b4b43b7bed62b5cf6e6b0b5ff5846270535/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf
  ${SINT_FONT_FILE}
  EXPECTED_HASH SHA256=478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823
)
set(SINT_FONT_SOURCE ${CMAKE_BINARY_DIR}/embeddedFont.cpp)
if(NOT EXISTS ${SINT_FONT_SOURCE})
  file(READ ${SINT_FONT_FILE} FONT_HEX HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
  file(WRITE ${SINT_FONT_SOURCE}
    "extern const unsigned char embeddedFont[] = {${FONT_BYTES}};\n"
    "extern const unsigned int embeddedFontSize = sizeof(embeddedFont);\n")
endif()
