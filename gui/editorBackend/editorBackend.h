#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "../../engine/command/command.h"
#include "../../engine/moduleRegistry/moduleRegistry.h"
#include "../../engine/parameterDescriptor/parameterDescriptor.h"
#include "../../engine/presetLibrary/presetLibrary.h"
#include "../../engine/synthModel/synthModel.h"

/**
 * Всё, что редактор знает о плагине (D-015): читает снимок модели и шлёт команды, к DSP доступа нет.
 * Как контракт между фронтендом и API: редактор можно заменить, не трогая движок.
 * Всё вызывается на главном потоке
 */
class EditorBackend {
  public:
    virtual ~EditorBackend() = default;

    /**
     * Текущая модель. Ссылка действительна до следующего вызова команды
     */
    virtual const SynthModel& getModel() = 0;

    /**
     * Растёт с каждым изменением модели, откуда бы оно ни пришло: от редактора, хоста или проекта
     */
    virtual uint64_t getRevision() = 0;

    virtual const ModuleRegistry& getRegistry() = 0;
    virtual CommandResult apply(const Command& command) = 0;

    /**
     * Начало и конец движения ручки: хост записывает между ними автоматизацию
     */
    virtual void beginGesture(ModuleId module, const std::string& parameter) = 0;
    virtual void endGesture(ModuleId module, const std::string& parameter) = 0;

    /**
     * Значение, к которому возвращается сброс ручки (D-061)
     */
    virtual float getDefaultValue(ModuleId module, const ParameterDescriptor& parameter) = 0;

    /**
     * Виден ли параметр хосту. Если слотов не хватило, он работает, но не автоматизируется (D-011)
     */
    virtual bool isExported(ModuleId module, const std::string& parameter) = 0;

    virtual std::string getPresetName() = 0;
    virtual std::vector<std::string> listPresets() = 0;
    virtual std::expected<void, std::string> loadPreset(const std::string& name) = 0;
    virtual std::expected<void, std::string> loadDefaultPreset() = 0;
    virtual std::expected<void, std::string> savePreset(const std::string& name, PRESET_WRITE mode) = 0;
};
