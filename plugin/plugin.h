#include "../clap/entry.h"
#include "../clap/factory/plugin-factory.h"
#include "../clap/plugin-features.h"
#include "../clap/version.h"
#include "../clap/clap.h"
#include "../engine/generatoir/genetaror.h"

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

  uint32_t latency;

  float sample_rate; // Частота дискретизации

  Generator gen;
};

// Методы CLAP
bool my_plugin_init(const struct clap_plugin *plugin);
const void* my_plugin_get_extension(const struct clap_plugin *plugin, const char *id);
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

extern const clap_plugin_audio_ports_t s_my_plug_audio_ports;
extern const clap_plugin_note_ports_t s_my_plug_note_ports;
extern const clap_plugin_latency_t s_my_plug_latency;
extern const clap_plugin_state_t s_my_plug_state;