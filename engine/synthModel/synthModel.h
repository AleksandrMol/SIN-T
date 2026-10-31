#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../command/command.h"
#include "../modelTypes/modelTypes.h"
#include "../moduleRegistry/moduleRegistry.h"

/**
 * Модель синтезатора: модули, соединения и маршруты модуляции. Только данные, без DSP.
 *
 * Хранилище плоское: модули подграфов лежат в той же таблице, что и модули главного графа,
 * и отличаются полем owner. Поэтому все графы проверяются одним кодом, а модель
 * копируется целиком: загрузка пресета собирает копию и потом подменяет текущую (D-020).
 *
 * Используется только на главном потоке (D-023). Изменяется только через apply (D-030).
 */
class SynthModel;

struct ModelTransition;

/**
 * Собирает переход от одной модели к другой (D-053)
 */
ModelTransition mergeModels(const SynthModel& previous, const SynthModel& next);

class SynthModel {
  public:
    /**
     * Реестр должен жить дольше модели
     */
    explicit SynthModel(const ModuleRegistry* registry);

    /**
     * Выполняет команду. При ошибке модель не меняется
     */
    CommandResult apply(const Command& command);

    const std::map<ModuleId, ModuleInstance>& getModules() const;
    const std::vector<Connection>& getConnections() const;

    /**
     * Маршруты в порядке создания: от него зависит разрыв циклов модуляции (D-010)
     */
    const std::vector<ModRoute>& getRoutes() const;

    ModuleId getOutputId() const;

    /**
     * ID, который получит следующий модуль. Сохраняется в пресете, чтобы ID не повторялись
     */
    ModuleId getNextId() const;

    /**
     * Возвращает модуль или nullptr, если такого нет
     */
    const ModuleInstance* findModule(ModuleId id) const;

    /**
     * Проверяет соединение и маршрут, ничего не меняя: редактор подсвечивает допустимые цели,
     * пока тянется провод. Команды проверяются этими же правилами
     */
    std::expected<void, std::string> checkConnection(const Connection& connection) const;
    std::expected<void, std::string> checkRoute(const ModSource& source, const ModTarget& target) const;

    bool operator==(const SynthModel&) const = default;

  private:
    // Загрузка собирает новую модель, а не меняет существующую: ей нужно восстановить модули
    // с их постоянными ID (D-011). Соединения и маршруты она проводит через обычные команды
    friend std::expected<SynthModel, std::string> loadModel(const ModuleRegistry* registry, std::string_view text);

    // Схема перехода держит то, что уходит: этого нет ни в одной модели, которую могут собрать команды
    friend ModelTransition mergeModels(const SynthModel& previous, const SynthModel& next);

    const ModuleRegistry* registry;
    std::map<ModuleId, ModuleInstance> modules;
    std::vector<Connection> connections;
    std::vector<ModRoute> routes;
    ModuleId outputId;
    ModuleId nextId = 1;

    ModuleId createModule(const ModuleDescriptor* descriptor, std::optional<ModuleId> owner);
    void insertModule(ModuleId id, const ModuleDescriptor* descriptor, std::optional<ModuleId> owner);
    bool hasAudioPath(ModuleId from, ModuleId to) const;
    std::vector<ModRoute>::iterator findRoute(const ModSource& source, const ModTarget& target);

    CommandResult execute(const AddModule& command);
    CommandResult execute(const RemoveModule& command);
    CommandResult execute(const Connect& command);
    CommandResult execute(const Disconnect& command);
    CommandResult execute(const SetParameter& command);
    CommandResult execute(const SetModuleEnabled& command);
    CommandResult execute(const SetModulePosition& command);
    CommandResult execute(const AddRoute& command);
    CommandResult execute(const RemoveRoute& command);
    CommandResult execute(const SetRouteDepth& command);
};

/**
 * Порядковый номер модуля среди модулей того же типа в том же графе
 */
uint32_t findModuleNumber(const SynthModel& model, const ModuleInstance& module);

/**
 * Имя модуля для человека: «Filter 1». Номер сдвигается после удаления соседа (D-057)
 */
std::string makeModuleLabel(const SynthModel& model, const ModuleInstance& module);

/**
 * Переход между двумя моделями. Удалённое ещё затухает, поэтому движок какое-то время исполняет обе сразу
 */
struct ModelTransition {
  // Обе модели сразу. Модуль, отключённый в новой модели, пока исполняется
  SynthModel scheme;

  // Что останется после перехода. Новая модель без проводов, которые замкнули бы цикл с уходящими:
  // такие провода дожидаются следующего перехода, потому что аудиограф циклов не допускает (D-007)
  SynthModel settled;
};
