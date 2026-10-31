#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../builtinModules/builtinModules.h"
#include "../modelTypes/modelTypes.h"
#include "../synthModel/synthModel.h"

// Список для хоста постоянный: макросы и пул. Пул можно только увеличивать (D-011)
constexpr uint32_t POOL_SLOT_COUNT = 1024;
constexpr uint32_t SLOT_COUNT = MACRO_COUNT + POOL_SLOT_COUNT;

/**
 * Что занимает слот. Это данные состояния проекта: ключи постоянные, индексов и указателей нет.
 * Тип нужен, потому что в другом пресете под тем же ID может оказаться другой модуль
 */
struct SlotBinding {
  ModuleId module;
  std::string type;
  std::string parameter;

  bool operator==(const SlotBinding&) const = default;
};

/**
 * Занятый слот так, как его видит хост
 */
struct SlotInfo {
  const ParameterDescriptor* descriptor;
  SlotTarget target;
  std::string name;
  std::string path;

  bool operator==(const SlotInfo&) const = default;
};

/**
 * Таблица «слот → модуль и параметр» (D-011). Хост видит постоянный список из SLOT_COUNT
 * параметров, таблица решает, что сейчас стоит за каждым номером. Номер слота — это clap_id.
 * Используется только на главном потоке, аудиопоток видит копию адресов в плане
 */
class SlotTable {
  public:
    SlotTable();

    /**
     * Приводит таблицу в соответствие с моделью: слоты исчезнувших параметров освобождаются,
     * новые параметры занимают первые свободные. Параметр, которому слота не хватило,
     * работает, но не экспортируется. Заглушки слотов не занимают: их параметры не меняются (D-021)
     */
    void sync(const SynthModel& model);

    /**
     * Занимает слот пула сохранённой записью. Перед использованием нужен sync: он уберёт
     * записи, которых нет в модели. Возвращает false, если номер не из пула или уже занят
     */
    bool restore(uint32_t slot, SlotBinding binding);

    /**
     * Занятый слот или nullptr
     */
    const SlotInfo* find(uint32_t slot) const;

    /**
     * Занятые слоты пула: именно они записываются в состояние, макросы постоянны
     */
    std::map<uint32_t, SlotBinding> getPoolBindings() const;

    /**
     * Адреса всех слотов для плана
     */
    std::vector<SlotTarget> getTargets() const;

    /**
     * Слоты, которые раньше занимал другой параметр: их автоматизация в хосте больше не относится к делу
     */
    std::vector<uint32_t> findReplaced(const SlotTable& previous) const;

    /**
     * Совпадает ли то, что показано хосту: имена, пути и видимость
     */
    bool hasSameInfo(const SlotTable& other) const;

    bool operator==(const SlotTable&) const = default;

  private:
    std::map<uint32_t, SlotBinding> bindings;
    std::map<uint32_t, SlotInfo> infos;
};
