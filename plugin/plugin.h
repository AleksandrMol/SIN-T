#pragma once
#include "../clap/entry.h"
#include "../clap/factory/plugin-factory.h"
#include "../clap/plugin-features.h"
#include "../clap/version.h"
#include "../clap/clap.h"
#include "../engine/engine/engine.h"
#include "../engine/moduleRegistry/moduleRegistry.h"
#include "../engine/sessionState/sessionState.h"
#include "../engine/slotTable/slotInbox.h"
#include "../engine/slotTable/slotTable.h"
#include "../engine/synthModel/synthModel.h"

#include <optional>
#include <string>

// Фабрика плагина
extern const clap_plugin_factory s_my_plugin_factory;

// ID плагина (Не должен меняться между версиями)
extern const char* const MY_PLUGIN_ID;

//Дескриптор
extern const clap_plugin_descriptor s_my_plugin_desc;

// Объект плагина
struct MyPluginInstance {
  // Первым полем ВСЕГДА И ОБЯЗАТЕЛЬНО идёт стандартный плагин CLAP
  clap_plugin plugin;

  // Далее можно указывать любые свои собственные переменные
  const clap_host_t* host; // Указатель на DAW
  const clap_host_params_t* hostParams; // Расширения хоста, могут отсутствовать
  const clap_host_preset_load_t* hostPresetLoad;

  uint32_t latency;

  float sample_rate; // Частота дискретизации
  bool isActive; // Между activate и deactivate. От него зависит, в каком потоке приходит params.flush

  ModuleRegistry registry;
  std::optional<SynthModel> model;
  SlotTable slots;
  SlotInbox slotInbox;
  std::string presetName; // Имя загруженного пресета, в состоянии проекта (D-020)
  Engine engine;
};

// Методы CLAP
bool my_plugin_init(const struct clap_plugin *plugin);
bool my_plugin_activate(
  const struct clap_plugin *plugin,
  double sample_rate,
  uint32_t min_frames_count,
  uint32_t max_frames_count
);
bool my_plugin_start_processing(const struct clap_plugin *plugin);
void my_plugin_stop_processing(const struct clap_plugin *plugin);
void my_plugin_deactivate(const struct clap_plugin *plugin);
void my_plug_reset(const struct clap_plugin *plugin);
clap_process_status my_plugin_process(
  const struct clap_plugin *plugin, 
  const clap_process_t *process
);
void my_plugin_destroy(const struct clap_plugin *plugin);
void my_plugin_on_main_thread(const struct clap_plugin *plugin);

// EXT-S
extern const clap_plugin_audio_ports_t s_my_plug_audio_ports;
extern const clap_plugin_note_ports_t s_my_plug_note_ports;
extern const clap_plugin_latency_t s_my_plug_latency;
extern const clap_plugin_state_t s_my_plug_state;
extern const clap_plugin_params_t s_my_plug_params;
extern const clap_plugin_preset_load_t s_my_plug_preset_load;
const void* my_plugin_get_extension(const struct clap_plugin *plugin, const char *id);

// EVENTS
void my_plug_process_event(MyPluginInstance *instance, const clap_event_header_t *event_header);
void my_plug_apply_transport(MyPluginInstance *instance, const clap_event_transport_t *transport);

// ПАРАМЕТРЫ
void my_plug_set_slot(MyPluginInstance *instance, const clap_event_param_value_t *event);
void my_plug_request_slot_sync(MyPluginInstance *instance);
void my_plug_sync_slot_values(MyPluginInstance *instance);
void my_plug_replace_state(MyPluginInstance *instance, SessionState state);
