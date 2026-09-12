#include "../clap/entry.h"
#include "../clap/factory/plugin-factory.h"
#include "../clap/plugin-features.h"
#include "../clap/version.h"
#include "../clap/clap.h"

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
  float sample_rate; // Частота дискретизации

  bool is_note_on;      // Зажата ли сейчас нота?
  int32_t active_note;  // Номер MIDI ноты (0-127)
  float phase;          // Текущая фаза осциллятора (от 0.0 до 1.0)
  float phase_step;     // На сколько сдвигать фазу с каждым сэмплом (зависит от частоты ноты)
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
clap_process_status my_plugin_process(
  const struct clap_plugin *plugin, 
  const clap_process_t *process
);
void my_plugin_destroy(const struct clap_plugin *plugin);

void my_plugin_on_main_thread(const struct clap_plugin *plugin);