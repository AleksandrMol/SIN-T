cmake_minimum_required(VERSION 3.24)
project(sin_t CXX)

option(SINT_BUILD_TESTS "Собирать unit-тесты (doctest скачивается при настройке)" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MSVC)
  # Исходники в UTF-8 с русскими комментариями, без /utf-8 MSVC читает их в кодировке системы
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# DSP и модель: без CLAP и без сторонних библиотек (D-002)
add_library(sint_engine STATIC
  engine/envelope/envelope.cpp
  engine/filter/filter.cpp
  engine/generator/generator.cpp
  engine/lfo/lfo.cpp
  engine/oscillator/oscillator.cpp
  engine/parameter/parameter.cpp
  engine/parameterRegistry/parameterRegistry.cpp
  engine/voice/voice.cpp
  tools/tools.cpp
)
target_include_directories(sint_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Слой CLAP. Объектная библиотека: те же файлы идут и в .clap, и в офлайн-рендер с тестами
add_library(sint_plugin_objects OBJECT
  plugin/descriptor.cpp
  plugin/entry.cpp
  plugin/events.cpp
  plugin/extensions.cpp
  plugin/factory.cpp
  plugin/methods.cpp
  plugin/process.cpp
)
target_link_libraries(sint_plugin_objects PUBLIC sint_engine)
if(NOT MSVC)
  # Сигнатуры колбэков задаёт CLAP, часть их параметров нам не нужна
  target_compile_options(sint_plugin_objects PRIVATE -Wno-unused-parameter)
endif()

# Готовый плагин. Установка: cmake --install build --prefix ~/.clap
add_library(sint MODULE $<TARGET_OBJECTS:sint_plugin_objects>)
target_link_libraries(sint PRIVATE sint_engine)
set_target_properties(sint PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "sint")
if(NOT WIN32)
  target_link_options(sint PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
endif()
install(TARGETS sint LIBRARY DESTINATION .)

# Хост без DAW: запускает плагин через CLAP и отдаёт звук в память
add_library(sint_offline STATIC
  tools/offlineHost/offlineHost.cpp
  tools/wav/wav.cpp
)
target_link_libraries(sint_offline PUBLIC sint_plugin_objects)

add_executable(sint_render tools/render/main.cpp)
target_link_libraries(sint_render PRIVATE sint_offline)

if(SINT_BUILD_TESTS)
  include(CTest)
  include(FetchContent)

  # Зависимость закреплена по версии и контрольной сумме
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)

  add_executable(sint_tests
    tests/main.cpp
    tests/clapPlugin.test.cpp
    tests/envelope.test.cpp
    tests/filter.test.cpp
    tests/oscillator.test.cpp
    tests/tools.test.cpp
    tests/wav.test.cpp
  )
  target_link_libraries(sint_tests PRIVATE sint_offline doctest::doctest)
  add_test(NAME sint_tests COMMAND sint_tests)
endif()
