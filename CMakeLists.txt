cmake_minimum_required(VERSION 3.28)
project(sin_t C CXX)

option(SINT_BUILD_TESTS "Собирать unit-тесты (doctest скачивается при настройке)" ON)
option(SINT_BUILD_GUI "Собирать редактор (RmlUi, pugl и FreeType скачиваются при настройке)" ON)
option(SINT_WERROR "Считать предупреждения в нашем коде ошибками" OFF)
set(SINT_SANITIZER "" CACHE STRING "Санитайзер для отдельной сборки: thread или address")

# Санитайзеры проверяют потоки движка, а библиотеки окна в их сборке только мешали бы
if(SINT_SANITIZER)
  set(SINT_BUILD_GUI OFF)
endif()

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

if(SINT_SANITIZER)
  add_compile_options(-fsanitize=${SINT_SANITIZER} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${SINT_SANITIZER})
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

if(SINT_BUILD_GUI)
  include(cmake/gui-dependencies.cmake)
endif()

# Наши строгие предупреждения: зависимости выше собираются со своими настройками
if(MSVC)
  # Исходники в UTF-8 с русскими комментариями, без /utf-8 MSVC читает их в кодировке системы
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
if(SINT_WERROR)
  add_compile_options($<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
endif()

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
  engine/macros/macrosProcessor.cpp
  engine/modulationCompiler/modulationCompiler.cpp
  engine/modulationPlan/modulationPlan.cpp
  engine/moduleCatalog/moduleCatalog.cpp
  engine/moduleDescriptor/moduleDescriptor.cpp
  engine/moduleRegistry/moduleRegistry.cpp
  engine/oscillator/oscillator.cpp
  engine/oscillator/oscillatorProcessor.cpp
  engine/parameterDescriptor/parameterDescriptor.cpp
  engine/planExchange/planExchange.cpp
  engine/preset/preset.cpp
  engine/presetLibrary/presetLibrary.cpp
  engine/presetMigration/presetMigration.cpp
  engine/sessionState/sessionState.cpp
  engine/slotTable/slotInbox.cpp
  engine/slotTable/slotOutbox.cpp
  engine/slotTable/slotTable.cpp
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
  plugin/editing.cpp
  plugin/entry.cpp
  plugin/events.cpp
  plugin/extensions.cpp
  plugin/factory.cpp
  plugin/methods.cpp
  plugin/parameters.cpp
  plugin/pluginEditorBackend.cpp
  plugin/process.cpp
)
target_link_libraries(sint_plugin_objects PUBLIC sint_engine)
if(NOT MSVC)
  # Сигнатуры колбэков задаёт CLAP, часть их параметров нам не нужна
  target_compile_options(sint_plugin_objects PRIVATE -Wno-unused-parameter)
endif()

# Сборки без окна: им не нужны X11 и OpenGL
add_library(sint_no_editor STATIC plugin/noEditor.cpp)
target_link_libraries(sint_no_editor PUBLIC sint_engine)
if(NOT MSVC)
  target_compile_options(sint_no_editor PRIVATE -Wno-unused-parameter)
endif()

if(SINT_BUILD_GUI)
  # Редактор без окна: его можно запустить в тестах на настоящем RmlUi, без дисплея и OpenGL
  add_library(sint_gui_view STATIC
    gui/editorDocument/editorDocument.cpp
    gui/editorOverlays/editorOverlays.cpp
    gui/editorScene/editorScene.cpp
    gui/editorView/editorView.cpp
    gui/elementTools/elementTools.cpp
    gui/graphLayout/graphLayout.cpp
    gui/knob/knob.cpp
    gui/logoImage/logoImage.cpp
    gui/meshBuilder/meshBuilder.cpp
    gui/metalTextures/metalTextures.cpp
    gui/plateDecorator/plateDecorator.cpp
    gui/rmlRuntime/rmlRuntime.cpp
    gui/wireGeometry/wireGeometry.cpp
    gui/wireLayer/wireLayer.cpp
    ${SINT_FONT_SOURCE}
  )
  target_link_libraries(sint_gui_view PUBLIC sint_engine RmlUi::Core)

  # Окно: pugl и OpenGL
  add_library(sint_gui_window STATIC
    gui/pluginWindow/pluginWindow.cpp
    ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp
  )
  target_include_directories(sint_gui_window PRIVATE ${rmlui_SOURCE_DIR}/Backends)
  target_link_libraries(sint_gui_window PUBLIC sint_gui_view pugl_static ${CMAKE_DL_LIBS})

  if(NOT MSVC)
    # Файл из RmlUi написан не под наши строгие предупреждения
    set_source_files_properties(${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp PROPERTIES COMPILE_OPTIONS "-w")
  endif()

  foreach(target sint_gui_view sint_gui_window)
    sint_apply_glibc_compat(${target})
  endforeach()

  add_library(sint_editor OBJECT plugin/editor.cpp)
  target_link_libraries(sint_editor PUBLIC sint_gui_window)
  if(NOT MSVC)
    target_compile_options(sint_editor PRIVATE -Wno-unused-parameter)
  endif()
  set(SINT_EDITOR_OBJECTS $<TARGET_OBJECTS:sint_editor>)
  set(SINT_EDITOR_LIBRARIES sint_gui_window)
else()
  set(SINT_EDITOR_OBJECTS "")
  set(SINT_EDITOR_LIBRARIES sint_no_editor)
endif()

# Готовый плагин. Установка: cmake --install build --prefix ~/.clap
add_library(sint MODULE $<TARGET_OBJECTS:sint_plugin_objects> ${SINT_EDITOR_OBJECTS})
target_link_libraries(sint PRIVATE sint_engine ${SINT_EDITOR_LIBRARIES})
set_target_properties(sint PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "sint")
if(NOT WIN32)
  target_link_options(sint PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
endif()
install(TARGETS sint LIBRARY DESTINATION .)

# Хост без DAW: запускает плагин через CLAP и отдаёт звук в память
add_library(sint_offline STATIC
  tools/goldenScenarios/goldenScenarios.cpp
  tools/offlineHost/offlineHost.cpp
  tools/wav/wav.cpp
)
target_link_libraries(sint_offline PUBLIC sint_plugin_objects sint_no_editor)

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
    tests/golden.test.cpp
    tests/graphLayout.test.cpp
    tests/liveChanges.test.cpp
    tests/modulation.test.cpp
    tests/modulators.test.cpp
    tests/moduleRegistry.test.cpp
    tests/oscillator.test.cpp
    tests/parameterDescriptor.test.cpp
    tests/parameters.test.cpp
    tests/polyphony.test.cpp
    tests/preset.test.cpp
    tests/presetLibrary.test.cpp
    tests/presetMigration.test.cpp
    tests/slotTable.test.cpp
    tests/synthModel.test.cpp
    tests/tools.test.cpp
    tests/voicePool.test.cpp
    tests/wav.test.cpp
    gui/graphLayout/graphLayout.cpp
    gui/wireGeometry/wireGeometry.cpp
  )
  target_link_libraries(sint_tests PRIVATE sint_offline doctest::doctest nlohmann_json::nlohmann_json)
  target_compile_definitions(sint_tests PRIVATE
    SINT_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples"
    SINT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME sint_tests COMMAND sint_tests)

  # Эталоны звука обновляются только этой командой, после сборки текущего кода (D-072):
  # cmake --build build --target update_golden
  add_executable(sint_update_golden tools/updateGolden/main.cpp)
  target_link_libraries(sint_update_golden PRIVATE sint_offline)
  add_custom_target(update_golden COMMAND sint_update_golden ${CMAKE_CURRENT_SOURCE_DIR} USES_TERMINAL)

  # Плагин загрузится в flatpak-Reaper (D-076). С санитайзером он туда не попадает.
  # Две проверки ниже следят, что скрипт умеет падать: зелёный результат без них ничего не значит
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT SINT_SANITIZER)
    set(SINT_CHECK_VERSIONS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/checkLibVersions.sh)
    add_test(NAME sint_lib_versions COMMAND ${SINT_CHECK_VERSIONS} $<TARGET_FILE:sint>)
    add_test(NAME sint_lib_versions_too_new COMMAND ${SINT_CHECK_VERSIONS} $<TARGET_FILE:sint> GLIBC=2.3)
    set_tests_properties(sint_lib_versions_too_new PROPERTIES PASS_REGULAR_EXPRESSION "GLIBC_[0-9.]+ новее")
    add_test(NAME sint_lib_versions_unreadable COMMAND ${SINT_CHECK_VERSIONS} ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
    set_tests_properties(sint_lib_versions_unreadable PROPERTIES PASS_REGULAR_EXPRESSION "не удалось прочитать")
  endif()

  if(SINT_BUILD_GUI)
    # Настоящий редактор и настоящий плагин, вместо окна пустая отрисовка
    add_executable(sint_gui_tests
      tests/gui/main.cpp
      tests/gui/editor.test.cpp
      tests/gui/logoImage.test.cpp
      tests/gui/meshBuilder.test.cpp
      tests/gui/metalTextures.test.cpp
      tests/gui/plateDecorator.test.cpp
      tests/gui/viewUpdate.test.cpp
    )
    target_link_libraries(sint_gui_tests PRIVATE sint_gui_view sint_plugin_objects sint_no_editor doctest::doctest)
    add_test(NAME sint_gui_tests COMMAND sint_gui_tests)

    # Открывает окно редактора и сохраняет кадр: так внешний вид проверяется без DAW
    add_executable(sint_editor_preview tools/editorPreview/main.cpp tools/editorBench/largePatch.cpp $<TARGET_OBJECTS:sint_editor>)
    target_link_libraries(sint_editor_preview PRIVATE sint_plugin_objects sint_gui_window X11::X11)

    # Замер редактора на большой схеме без окна (D-079)
    add_executable(sint_editor_bench tools/editorBench/main.cpp tools/editorBench/largePatch.cpp)
    target_link_libraries(sint_editor_bench PRIVATE sint_gui_view sint_plugin_objects sint_no_editor)
  endif()
endif()
