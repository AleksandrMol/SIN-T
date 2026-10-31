#include "./graphLayout.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
  constexpr float MARGIN = 24.0f;
  constexpr float COLUMN_WIDTH = 290.0f;
  constexpr float ROW_HEIGHT = 200.0f;

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

  std::map<uint32_t, uint32_t> rowsInColumn;
  std::map<ModuleId, ModulePosition> positions;
  uint32_t rowCount = 0;

  for (const ModuleId id : audio) {
    const bool isOutput = model.findModule(id)->descriptor->role == MODULE_ROLE::OUTPUT;
    const uint32_t column = isOutput ? std::max(lastColumn, 1u) : columns[id];
    const uint32_t row = rowsInColumn[column]++;

    positions[id] = { MARGIN + (float)column * COLUMN_WIDTH, MARGIN + (float)row * ROW_HEIGHT };
    rowCount = std::max(rowCount, row + 1);
  }

  for (size_t index = 0; index < modulators.size(); index++) {
    positions[modulators[index]] = { MARGIN + (float)index * COLUMN_WIDTH, MARGIN + (float)rowCount * ROW_HEIGHT };
  }

  return positions;
}
