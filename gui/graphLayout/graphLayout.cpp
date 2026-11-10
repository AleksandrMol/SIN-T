#include "./graphLayout.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../../engine/oscillator/oscillatorProcessor.h"

namespace {
  constexpr float MARGIN = 24.0f;
  constexpr float COLUMN_WIDTH = 290.0f;
  constexpr float ROW_GAP = 24.0f;

  // Размеры блока из стилей редактора, dp (editorDocument.cpp)
  constexpr uint32_t WIDE_BLOCK_KNOBS = 3;
  constexpr uint32_t KNOBS_PER_ROW = 4; // В узком блоке ручек меньше трёх: и там они встают в один ряд
  constexpr float FRAME_HEIGHT = 27.0f; // Рамка сверху и снизу, шапка и шов под ней
  constexpr float BODY_PADDING = 12.0f; // У тела без ручек высота только из отступов
  constexpr float KNOB_ROW_HEIGHT = 72.0f;
  constexpr float OPEN_BUTTON_HEIGHT = 34.0f;
  constexpr float STUB_NOTE_HEIGHT = 28.0f;
  constexpr float OSCILLATOR_BODY_HEIGHT = 216.0f; // Своя лицевая сторона (D-108)

  bool isOnCanvas(const ModuleInstance& module, std::optional<ModuleId> graph) {
    return module.owner == graph && module.descriptor->role != MODULE_ROLE::MACROS;
  }

  bool hasAudioPorts(const ModuleInstance& module) {
    return !module.descriptor->inputs.empty() || !module.descriptor->outputs.empty();
  }

  /**
   * Длина самого длинного пути от источника. Циклов в графе нет (D-007), поэтому хватает числа
   * проходов, равного числу модулей
   */
  std::map<ModuleId, uint32_t> computeColumns(const SynthModel& model, const std::vector<ModuleId>& modules) {
    std::map<ModuleId, uint32_t> columns;

    for (const ModuleId id : modules) {
      columns[id] = 0;
    }

    for (size_t pass = 0; pass < modules.size(); pass++) {
      for (const Connection& connection : model.getConnections()) {
        const auto from = columns.find(connection.fromModule);
        const auto to = columns.find(connection.toModule);

        if (from != columns.end() && to != columns.end()) {
          to->second = std::max(to->second, from->second + 1);
        }
      }
    }

    return columns;
  }
}

bool isSmallBlock(const ModuleInstance& module) {
  return module.isStub() || (module.descriptor->parameters.size() < WIDE_BLOCK_KNOBS && module.descriptor->role != MODULE_ROLE::GENERATOR);
}

float getBlockHeight(const ModuleInstance& module) {
  const ModuleDescriptor& descriptor = *module.descriptor;

  if (module.isStub()) {
    return FRAME_HEIGHT + BODY_PADDING + STUB_NOTE_HEIGHT;
  }

  // Своя лицевая сторона (D-108), её строит EditorScene::buildOscillatorFace
  if (descriptor.type == OSCILLATOR_DESCRIPTOR.type) {
    return FRAME_HEIGHT + OSCILLATOR_BODY_HEIGHT;
  }

  const uint32_t rows = ((uint32_t)descriptor.parameters.size() + KNOBS_PER_ROW - 1) / KNOBS_PER_ROW;
  const float body = BODY_PADDING + (float)rows * KNOB_ROW_HEIGHT;

  return FRAME_HEIGHT + body + (descriptor.role == MODULE_ROLE::GENERATOR ? OPEN_BUTTON_HEIGHT : 0.0f);
}

std::map<ModuleId, ModulePosition> computeLayout(const SynthModel& model, std::optional<ModuleId> graph) {
  std::vector<ModuleId> audio;
  std::vector<ModuleId> modulators;

  for (const auto& [id, module] : model.getModules()) {
    if (isOnCanvas(module, graph)) {
      (hasAudioPorts(module) ? audio : modulators).push_back(id);
    }
  }

  std::map<ModuleId, uint32_t> columns = computeColumns(model, audio);
  uint32_t lastColumn = 0;

  for (const auto& [id, column] : columns) {
    lastColumn = std::max(lastColumn, column);
  }

  std::map<uint32_t, float> columnTops;
  std::map<ModuleId, ModulePosition> positions;
  float modulatorTop = MARGIN;

  for (const ModuleId id : audio) {
    const ModuleInstance& module = *model.findModule(id);
    const bool isOutput = module.descriptor->role == MODULE_ROLE::OUTPUT;
    const uint32_t column = isOutput ? std::max(lastColumn, 1u) : columns[id];
    const auto [top, isNew] = columnTops.try_emplace(column, MARGIN);

    positions[id] = { MARGIN + (float)column * COLUMN_WIDTH, top->second };
    top->second += getBlockHeight(module) + ROW_GAP;
    modulatorTop = std::max(modulatorTop, top->second);
  }

  for (size_t index = 0; index < modulators.size(); index++) {
    positions[modulators[index]] = { MARGIN + (float)index * COLUMN_WIDTH, modulatorTop };
  }

  return positions;
}
