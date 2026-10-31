#pragma once

#include "../gui/editorBackend/editorBackend.h"
#include "./plugin.h"

/**
 * Редактор глазами плагина: команды идут в my_plug_apply_command, снимок — это сама модель,
 * потому что редактор и модель живут на главном потоке (D-015, D-067)
 */
class PluginEditorBackend final : public EditorBackend {
  public:
    explicit PluginEditorBackend(MyPluginInstance* instance);

    const SynthModel& getModel() override;
    uint64_t getRevision() override;
    const ModuleRegistry& getRegistry() override;
    CommandResult apply(const Command& command) override;
    void beginGesture(ModuleId module, const std::string& parameter) override;
    void endGesture(ModuleId module, const std::string& parameter) override;
    float getDefaultValue(ModuleId module, const ParameterDescriptor& parameter) override;
    bool isExported(ModuleId module, const std::string& parameter) override;
    std::string getPresetName() override;
    std::vector<std::string> listPresets() override;
    std::expected<void, std::string> loadPreset(const std::string& name) override;
    std::expected<void, std::string> loadDefaultPreset() override;
    std::expected<void, std::string> savePreset(const std::string& name, PRESET_WRITE mode) override;

  private:
    MyPluginInstance* instance;
};
