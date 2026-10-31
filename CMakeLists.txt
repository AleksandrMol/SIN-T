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

include(FetchContent)

# Зависимости закреплены по версии и контрольной сумме
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# DSP и модель. DSP без сторонних библиотек (D-002), JSON нужен только пресетам (D-020)
add_library(sint_engine STATIC
  engine/amp/ampProcessor.cpp
  engine/builtinModules/builtinModules.cpp
  engine/defaultPreset/defaultPreset.cpp
  engine/engine/engine.cpp
  engine/env/envProcessor.cpp
  engine/executionPlan/executionPlan.cpp
  engine/filter/filter.cpp
  engine/filter/filterProcessor.cpp
  engine/generator/generatorProcessor.cpp
  engine/lfo/lfoProcessor.cpp
  engine/modulationCompiler/modulationCompiler.cpp
  engine/modulationPlan/modulationPlan.cpp
  engine/moduleCatalog/moduleCatalog.cpp
  engine/moduleDescriptor/moduleDescriptor.cpp
  engine/moduleRegistry/moduleRegistry.cpp
  engine/oscillator/oscillator.cpp
  engine/oscillator/oscillatorProcessor.cpp
  engine/parameterDescriptor/parameterDescriptor.cpp
  engine/preset/preset.cpp
  engine/stubProcessor/stubProcessor.cpp
  engine/synthModel/synthModel.cpp
  engine/voicePool/voicePool.cpp
  tools/tools.cpp
)
target_include_directories(sint_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sint_engine PRIVATE nlohmann_json::nlohmann_json)

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
    tests/allocationCounter.cpp
    tests/clapPlugin.test.cpp
    tests/engine.test.cpp
    tests/filter.test.cpp
    tests/modulation.test.cpp
    tests/modulators.test.cpp
    tests/moduleRegistry.test.cpp
    tests/oscillator.test.cpp
    tests/parameterDescriptor.test.cpp
    tests/polyphony.test.cpp
    tests/preset.test.cpp
    tests/synthModel.test.cpp
    tests/tools.test.cpp
    tests/voicePool.test.cpp
    tests/wav.test.cpp
  )
  target_link_libraries(sint_tests PRIVATE sint_offline doctest::doctest nlohmann_json::nlohmann_json)
  target_compile_definitions(sint_tests PRIVATE SINT_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
  add_test(NAME sint_tests COMMAND sint_tests)
endif()
