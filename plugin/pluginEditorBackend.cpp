#include "./pluginEditorBackend.h"

#include "../engine/defaultPreset/defaultPreset.h"
#include "../engine/preset/preset.h"

PluginEditorBackend::PluginEditorBackend(MyPluginInstance* instance) : instance(instance) {}

const SynthModel& PluginEditorBackend::getModel() {
  return *this->instance->model;
}

uint64_t PluginEditorBackend::getRevision() {
  // Автоматизация могла ещё не дойти до модели: ручки должны двигаться вслед за ней
  my_plug_sync_slot_values(this->instance);

  return this->instance->modelRevision;
}

const ModuleRegistry& PluginEditorBackend::getRegistry() {
  return this->instance->registry;
}

CommandResult PluginEditorBackend::apply(const Command& command) {
  return my_plug_apply_command(this->instance, command);
}

void PluginEditorBackend::beginGesture(ModuleId module, const std::string& parameter) {
  my_plug_begin_gesture(this->instance, module, parameter);
}

void PluginEditorBackend::endGesture(ModuleId module, const std::string& parameter) {
  my_plug_end_gesture(this->instance, module, parameter);
}

float PluginEditorBackend::getDefaultValue(ModuleId module, const ParameterDescriptor& parameter) {
  const ModuleInstance* loaded = this->instance->loadedModel->findModule(module);

  return loaded ? loaded->parameters.at(parameter.key) : parameter.defaultValue;
}

bool PluginEditorBackend::isExported(ModuleId module, const std::string& parameter) {
  return this->instance->slots.findSlot(module, parameter).has_value();
}

std::string PluginEditorBackend::getPresetName() {
  return this->instance->presetName;
}

std::vector<std::string> PluginEditorBackend::listPresets() {
  return this->instance->presetLibrary ? this->instance->presetLibrary->list() : std::vector<std::string>{};
}

std::expected<void, std::string> PluginEditorBackend::loadPreset(const std::string& name) {
  if (!this->instance->presetLibrary) {
    return std::unexpected("система не сообщает домашнюю папку");
  }

  const auto text = this->instance->presetLibrary->read(name);

  if (!text) {
    return std::unexpected(text.error());
  }

  return my_plug_load_preset(this->instance, *text, name);
}

std::expected<void, std::string> PluginEditorBackend::loadDefaultPreset() {
  const auto model = createDefaultModel(&this->instance->registry);

  if (!model) {
    return std::unexpected(model.error());
  }

  return my_plug_load_preset(this->instance, saveModel(*model), "");
}

std::expected<void, std::string> PluginEditorBackend::savePreset(const std::string& name, PRESET_WRITE mode) {
  return my_plug_save_preset(this->instance, name, mode);
}
