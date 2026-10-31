#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "../clap/clap.h"
#include "../engine/defaultPreset/defaultPreset.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/preset/preset.h"
#include "../engine/slotTable/slotTable.h"
#include "../engine/synthModel/synthModel.h"
#include "./allocationCounter.h"

using json = nlohmann::json;

namespace {
  constexpr uint32_t SAMPLE_RATE = 48000;
  constexpr int16_t KEY_A4 = 69;
  constexpr int16_t KEY_C5 = 72;

  // Сглаживание значения и время затухания голоса: 10 мс
  constexpr uint32_t SMOOTHING_FRAMES = SAMPLE_RATE / 100;

  const clap_plugin_factory_t* getFactory() {
    return (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  }

  float peak(const std::vector<float>& channel) {
    float result = 0.0f;

    for (const float sample : channel) {
      result = std::fmax(result, std::fabs(sample));
    }

    return result;
  }

  clap_event_param_value_t makeValue(clap_id slot, double value, uint32_t time = 0) {
    clap_event_param_value_t event{};
    event.header.size = sizeof(clap_event_param_value_t);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = slot;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;

    return event;
  }

  clap_event_note_t makeNote(uint16_t type, int16_t key, uint32_t time = 0) {
    clap_event_note_t event{};
    event.header.size = sizeof(clap_event_note_t);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = type;
    event.note_id = -1;
    event.key = key;
    event.velocity = 1.0;

    return event;
  }

  ModuleId findVoiceOutput(const SynthModel& model, ModuleId generator) {
    for (const auto& [id, module] : model.getModules()) {
      if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
        return id;
      }
    }

    return 0;
  }

  /**
   * Пресет: синус через фильтр и amp. Громкость задаёт только макрос 1
   */
  std::string buildMacroPreset() {
    ModuleRegistry registry;
    REQUIRE(addAllModules(registry).has_value());

    SynthModel model(&registry);
    const ModuleId generator = **model.apply(AddModule{ "generator" });
    const ModuleId osc = **model.apply(AddModule{ "oscillator", generator });
    const ModuleId amp = **model.apply(AddModule{ "amp", generator });

    for (const Command& command : std::initializer_list<Command>{
      SetParameter{ osc, "wave", 1.0f },
      SetParameter{ amp, "gain", 0.0f },
      Connect{ { osc, "out", amp, "in" } },
      Connect{ { amp, "out", findVoiceOutput(model, generator), "in" } },
      Connect{ { generator, "out", model.getOutputId(), "in" } },
      AddRoute{ { { MACROS_MODULE_ID, "macro1" }, { amp, "gain" }, 1.0f } },
    }) {
      REQUIRE(model.apply(command).has_value());
    }

    return saveModel(model);
  }

  /**
   * Пресет с другим устройством: два осциллятора в подграфе
   */
  std::string buildTwoOscillatorPreset() {
    ModuleRegistry registry;
    REQUIRE(addAllModules(registry).has_value());

    SynthModel model(&registry);
    const ModuleId generator = **model.apply(AddModule{ "generator" });
    const ModuleId first = **model.apply(AddModule{ "oscillator", generator });
    const ModuleId second = **model.apply(AddModule{ "oscillator", generator });
    const ModuleId voiceOutput = findVoiceOutput(model, generator);

    for (const Command& command : std::initializer_list<Command>{
      Connect{ { first, "out", voiceOutput, "in" } },
      Connect{ { second, "out", voiceOutput, "in" } },
      Connect{ { generator, "out", model.getOutputId(), "in" } },
    }) {
      REQUIRE(model.apply(command).has_value());
    }

    return saveModel(model);
  }

  std::string writeTemporary(const std::string& name, const std::string& text) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;

    std::ofstream(path, std::ios::binary) << text;

    return path.string();
  }

  /**
   * Хост с расширениями параметров и пресетов, записывает всё, что плагин у него просит
   */
  struct ParamsFixture {
    clap_host_t host{};
    clap_host_params_t hostParams{};
    clap_host_preset_load_t hostPresets{};
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_preset_load_t* presets = nullptr;
    bool isActive = false;

    std::vector<clap_param_rescan_flags> rescans;
    std::vector<clap_id> cleared;
    std::vector<std::string> presetErrors;
    std::vector<std::string> loadedPresets;
    std::atomic<int> callbacks = 0;

    static ParamsFixture* self(const clap_host_t* host) {
      return (ParamsFixture*)host->host_data;
    }

    explicit ParamsFixture(bool hasExtensions = true) {
      this->host.clap_version = CLAP_VERSION;
      this->host.host_data = this;
      this->host.request_callback = [](const clap_host_t* host) {
        self(host)->callbacks++;
      };
      this->host.get_extension = hasExtensions
        ? [](const clap_host_t* host, const char* id) -> const void* {
            if (!std::strcmp(id, CLAP_EXT_PARAMS)) {
              return &self(host)->hostParams;
            }

            return !std::strcmp(id, CLAP_EXT_PRESET_LOAD) ? &self(host)->hostPresets : nullptr;
          }
        : [](const clap_host_t*, const char*) -> const void* { return nullptr; };

      this->hostParams.rescan = [](const clap_host_t* host, clap_param_rescan_flags flags) {
        self(host)->rescans.push_back(flags);
      };
      this->hostParams.clear = [](const clap_host_t* host, clap_id id, clap_param_clear_flags) {
        self(host)->cleared.push_back(id);
      };
      this->hostPresets.on_error = [](const clap_host_t* host, uint32_t, const char*, const char*, int32_t, const char* message) {
        self(host)->presetErrors.emplace_back(message);
      };
      this->hostPresets.loaded = [](const clap_host_t* host, uint32_t, const char* location, const char*) {
        self(host)->loadedPresets.emplace_back(location);
      };

      const clap_plugin_factory_t* factory = getFactory();

      this->plugin = factory->create_plugin(factory, &this->host, factory->get_plugin_descriptor(factory, 0)->id);

      REQUIRE(this->plugin != nullptr);
      REQUIRE(this->plugin->init(this->plugin));

      this->params = (const clap_plugin_params_t*)this->plugin->get_extension(this->plugin, CLAP_EXT_PARAMS);
      this->state = (const clap_plugin_state_t*)this->plugin->get_extension(this->plugin, CLAP_EXT_STATE);
      this->presets = (const clap_plugin_preset_load_t*)this->plugin->get_extension(this->plugin, CLAP_EXT_PRESET_LOAD);

      REQUIRE(this->params != nullptr);
      REQUIRE(this->state != nullptr);
      REQUIRE(this->presets != nullptr);
    }

    ~ParamsFixture() {
      this->stop();
      this->plugin->destroy(this->plugin);
    }

    void start() {
      REQUIRE(this->plugin->activate(this->plugin, SAMPLE_RATE, 1, 8192));
      REQUIRE(this->plugin->start_processing(this->plugin));
      this->isActive = true;
    }

    void stop() {
      if (this->isActive) {
        this->plugin->stop_processing(this->plugin);
        this->plugin->deactivate(this->plugin);
        this->isActive = false;
      }
    }

    void mainThread() {
      this->plugin->on_main_thread(this->plugin);
    }

    clap_param_info_t info(uint32_t index) const {
      clap_param_info_t result{};

      REQUIRE(this->params->get_info(this->plugin, index, &result));

      return result;
    }

    clap_id findSlot(const std::string& name) const {
      for (uint32_t index = 0; index < this->params->count(this->plugin); index++) {
        const clap_param_info_t current = this->info(index);

        if (name == current.name && !(current.flags & CLAP_PARAM_IS_HIDDEN)) {
          return current.id;
        }
      }

      FAIL("Нет параметра " << name);
      return 0;
    }

    double value(clap_id slot) const {
      double result = -1.0;

      REQUIRE(this->params->get_value(this->plugin, slot, &result));

      return result;
    }

    /**
     * Слоты, видимые хосту
     */
    uint32_t countVisible() const {
      uint32_t count = 0;

      for (uint32_t index = 0; index < this->params->count(this->plugin); index++) {
        count += this->info(index).flags & CLAP_PARAM_IS_HIDDEN ? 0 : 1;
      }

      return count;
    }

    bool load(const std::string& text) {
      struct Reader {
        const std::string& text;
        size_t offset;
      } reader = { text, 0 };

      const clap_istream_t stream = {
        .ctx = &reader,
        .read = [](const clap_istream_t* source, void* buffer, uint64_t size) -> int64_t {
          auto* current = (Reader*)source->ctx;
          const size_t count = std::min((size_t)size, current->text.size() - current->offset);

          std::memcpy(buffer, current->text.data() + current->offset, count);
          current->offset += count;

          return (int64_t)count;
        },
      };

      return this->state->load(this->plugin, &stream);
    }

    std::string save() {
      std::string text;

      const clap_ostream_t stream = {
        .ctx = &text,
        .write = [](const clap_ostream_t* target, const void* buffer, uint64_t size) -> int64_t {
          ((std::string*)target->ctx)->append((const char*)buffer, size);

          return (int64_t)size;
        },
      };

      REQUIRE(this->state->save(this->plugin, &stream));

      return text;
    }

    void flush(std::vector<const clap_event_header_t*> events) {
      const clap_input_events_t input = {
        .ctx = &events,
        .size = [](const clap_input_events_t* list) {
          return (uint32_t)((std::vector<const clap_event_header_t*>*)list->ctx)->size();
        },
        .get = [](const clap_input_events_t* list, uint32_t index) {
          return (*(std::vector<const clap_event_header_t*>*)list->ctx)[index];
        },
      };

      this->params->flush(this->plugin, &input, nullptr);
    }

    /**
     * Обрабатывает блок и возвращает левый канал
     */
    std::vector<float> process(uint32_t frames, std::vector<const clap_event_header_t*> events = {}) {
      std::vector<float> left(frames);
      std::vector<float> right(frames);
      float* channels[2] = { left.data(), right.data() };

      clap_audio_buffer_t output{};
      output.data32 = channels;
      output.channel_count = 2;

      const clap_input_events_t input = {
        .ctx = &events,
        .size = [](const clap_input_events_t* list) {
          return (uint32_t)((std::vector<const clap_event_header_t*>*)list->ctx)->size();
        },
        .get = [](const clap_input_events_t* list, uint32_t index) {
          return (*(std::vector<const clap_event_header_t*>*)list->ctx)[index];
        },
      };

      clap_process_t process{};
      process.frames_count = frames;
      process.audio_outputs = &output;
      process.audio_outputs_count = 1;
      process.in_events = &input;

      REQUIRE(this->plugin->process(this->plugin, &process) != CLAP_PROCESS_ERROR);

      return left;
    }

    /**
     * Звук нот и значений, заданных от начала рендера, блоками нужного размера
     */
    std::vector<float> render(uint32_t frames, uint32_t blockSize, std::vector<clap_event_note_t> notes, std::vector<clap_event_param_value_t> values) {
      std::vector<float> result;

      for (uint32_t offset = 0; offset < frames; offset += blockSize) {
        const uint32_t count = std::min(blockSize, frames - offset);
        std::vector<clap_event_note_t> blockNotes;
        std::vector<clap_event_param_value_t> blockValues;

        blockNotes.reserve(notes.size());
        blockValues.reserve(values.size());

        std::vector<const clap_event_header_t*> events;

        for (const clap_event_note_t& note : notes) {
          if (note.header.time >= offset && note.header.time < offset + count) {
            blockNotes.push_back(note);
            blockNotes.back().header.time -= offset;
            events.push_back(&blockNotes.back().header);
          }
        }

        for (const clap_event_param_value_t& value : values) {
          if (value.header.time >= offset && value.header.time < offset + count) {
            blockValues.push_back(value);
            blockValues.back().header.time -= offset;
            events.push_back(&blockValues.back().header);
          }
        }

        std::ranges::stable_sort(events, {}, [](const clap_event_header_t* event) { return event->time; });

        const std::vector<float> block = this->process(count, events);

        result.insert(result.end(), block.begin(), block.end());
      }

      return result;
    }
  };
}

TEST_CASE("Хост видит постоянный список: число параметров не зависит от пресета (D-011)") {
  ParamsFixture fixture;

  CHECK(fixture.params->count(fixture.plugin) == 1032);

  const uint32_t defaultVisible = fixture.countVisible();

  CHECK(defaultVisible > 8);
  CHECK(defaultVisible < 100);

  REQUIRE(fixture.load(buildTwoOscillatorPreset()));

  CHECK(fixture.params->count(fixture.plugin) == 1032);
  CHECK(fixture.countVisible() != defaultVisible);
}

TEST_CASE("Все слоты: постоянный ID, диапазон 0..1, автоматизация, без ступеней (D-011)") {
  ParamsFixture fixture;

  for (uint32_t index = 0; index < SLOT_COUNT; index++) {
    const clap_param_info_t info = fixture.info(index);

    CAPTURE(index);
    CHECK(info.id == index);
    CHECK(info.min_value == 0.0);
    CHECK(info.max_value == 1.0);
    CHECK(info.default_value >= 0.0);
    CHECK(info.default_value <= 1.0);
    CHECK((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0);
    CHECK((info.flags & CLAP_PARAM_IS_STEPPED) == 0);
    CHECK(info.cookie == nullptr);
    CHECK(info.name[0] != '\0');
  }

  clap_param_info_t info{};
  double value = 0.0;
  char text[64];

  CHECK_FALSE(fixture.params->get_info(fixture.plugin, SLOT_COUNT, &info));
  CHECK_FALSE(fixture.params->get_value(fixture.plugin, SLOT_COUNT, &value));
  CHECK_FALSE(fixture.params->value_to_text(fixture.plugin, SLOT_COUNT, 0.5, text, sizeof(text)));
  CHECK_FALSE(fixture.params->text_to_value(fixture.plugin, SLOT_COUNT, "0.5", &value));
}

TEST_CASE("Значение по умолчанию для хоста — то, с которым параметр загружен (D-061)") {
  ParamsFixture fixture;

  const auto checkDefaults = [&fixture] {
    for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
      CAPTURE(slot);
      CHECK(fixture.info(slot).default_value == fixture.value(slot));
    }
  };

  // В пресете по умолчанию gain равен 0, а не значению из описания amp: громкость задаёт ENV
  checkDefaults();
  CHECK(fixture.info(fixture.findSlot("G1 Amp 1 Gain")).default_value == 0.0);

  SUBCASE("Автоматизация значение по умолчанию не двигает") {
    const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");
    const double before = fixture.info(cutoff).default_value;
    const clap_event_param_value_t value = makeValue(cutoff, 0.25);

    fixture.flush({ &value.header });

    CHECK(fixture.value(cutoff) == doctest::Approx(0.25));
    CHECK(fixture.info(cutoff).default_value == before);
  }

  SUBCASE("Новое состояние приносит свои значения") {
    ParamsFixture other;
    const clap_id cutoff = other.findSlot("G1 Filter 1 Cutoff");
    const clap_event_param_value_t value = makeValue(cutoff, 0.25);

    other.flush({ &value.header });

    REQUIRE(fixture.load(other.save()));

    checkDefaults();
    CHECK(fixture.info(cutoff).default_value == doctest::Approx(0.25));
  }

  SUBCASE("Пресет из файла тоже") {
    const std::string path = writeTemporary("sint-test-defaults.sintpreset", buildTwoOscillatorPreset());

    REQUIRE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));

    checkDefaults();

    std::filesystem::remove(path);
  }
}

TEST_CASE("Пресет по умолчанию: макросы и параметры модулей видны под понятными именами") {
  ParamsFixture fixture;

  CHECK(std::string(fixture.info(0).name) == "Macro 1");
  CHECK(std::string(fixture.info(0).module) == "Macros");
  CHECK(std::string(fixture.info(7).name) == "Macro 8");

  const clap_param_info_t cutoff = fixture.info(fixture.findSlot("G1 Filter 1 Cutoff"));

  CHECK(std::string(cutoff.module) == "Generator 1/Filter 1");

  // Значение по умолчанию в списке совпадает с текущим
  CHECK(cutoff.default_value == doctest::Approx(fixture.value(cutoff.id)));

  const clap_param_info_t hidden = fixture.info(SLOT_COUNT - 1);

  CHECK((hidden.flags & CLAP_PARAM_IS_HIDDEN) != 0);
  CHECK(std::string(hidden.module).empty());
}

TEST_CASE("Значение слота как текст: реальные единицы и обратно (D-011)") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");
  char text[64];
  double value = -1.0;

  // Частота лежит на логарифмической кривой: 0 и 1 дают границы диапазона
  REQUIRE(fixture.params->value_to_text(fixture.plugin, cutoff, 1.0, text, sizeof(text)));
  CHECK(std::string(text) == "20000 Hz");

  REQUIRE(fixture.params->text_to_value(fixture.plugin, cutoff, "20000 Hz", &value));
  CHECK(value == doctest::Approx(1.0));

  CHECK_FALSE(fixture.params->text_to_value(fixture.plugin, cutoff, "много", &value));

  SUBCASE("Результат обрезается по буферу хоста") {
    char small[4];

    REQUIRE(fixture.params->value_to_text(fixture.plugin, cutoff, 1.0, small, sizeof(small)));
    CHECK(std::string(small) == "200");
    CHECK_FALSE(fixture.params->value_to_text(fixture.plugin, cutoff, 1.0, small, 0));
  }

  SUBCASE("Свободный слот работает со значением 0..1") {
    REQUIRE(fixture.params->value_to_text(fixture.plugin, SLOT_COUNT - 1, 0.25, text, sizeof(text)));
    REQUIRE(fixture.params->text_to_value(fixture.plugin, SLOT_COUNT - 1, text, &value));
    CHECK(value == doctest::Approx(0.25));
    CHECK_FALSE(fixture.params->text_to_value(fixture.plugin, SLOT_COUNT - 1, "x", &value));
  }

  SUBCASE("Перевод туда и обратно устойчив на всех видимых слотах") {
    for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
      for (const double position : { 0.0, 0.1, 0.37, 0.5, 0.93, 1.0 }) {
        char first[64];
        char second[64];

        CAPTURE(slot);
        REQUIRE(fixture.params->value_to_text(fixture.plugin, slot, position, first, sizeof(first)));
        REQUIRE(fixture.params->text_to_value(fixture.plugin, slot, first, &value));
        REQUIRE(fixture.params->value_to_text(fixture.plugin, slot, value, second, sizeof(second)));
        CHECK(std::string(first) == std::string(second));
      }
    }
  }
}

TEST_CASE("Автоматизация меняет звук, плавно и без щелчка (ТЗ §10, D-025)") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, 60);

  fixture.process(1000, { &on.header });

  const float loud = peak(fixture.process(4800));

  REQUIRE(loud > 0.01f);

  const clap_event_param_value_t low = makeValue(cutoff, 0.0);
  const std::vector<float> change = fixture.process(SMOOTHING_FRAMES * 2, { &low.header });

  // Значение ещё не дошло: сглаживание идёт ~10 мс, начало звучит почти как раньше
  CHECK(peak({ change.begin(), change.begin() + 64 }) > loud * 0.5f);
  CHECK(peak(fixture.process(4800)) < loud * 0.1f);
}

TEST_CASE("Автоматизация не зависит от размера блока хоста (D-008)") {
  const std::vector<clap_event_note_t> notes = { makeNote(CLAP_EVENT_NOTE_ON, 60, 10) };
  const auto play = [&notes](uint32_t blockSize) {
    ParamsFixture fixture;
    const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

    fixture.start();

    return fixture.render(4000, blockSize, notes, {
      makeValue(cutoff, 0.3, 500),
      makeValue(cutoff, 0.9, 701),
      makeValue(1, 0.5, 1234), // Макрос без маршрутов
      makeValue(cutoff, 0.1, 2900),
    });
  };

  const std::vector<float> reference = play(4000);

  CHECK(play(64) == reference);
  CHECK(play(33) == reference);
  CHECK(play(256) == reference);
  CHECK(play(1000) == reference);
}

TEST_CASE("Значение от хоста доходит до модели: get_value и состояние его видят сразу (D-056)") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

  fixture.start();

  const clap_event_param_value_t value = makeValue(cutoff, 0.25);

  fixture.process(256, { &value.header });

  CHECK(fixture.callbacks > 0);

  SUBCASE("get_value до on_main_thread") {
    CHECK(fixture.value(cutoff) == doctest::Approx(0.25));
  }

  SUBCASE("Сохранение до on_main_thread") {
    ParamsFixture restored;

    REQUIRE(restored.load(fixture.save()));
    CHECK(restored.value(cutoff) == doctest::Approx(0.25));
  }

  SUBCASE("После on_main_thread") {
    fixture.mainThread();
    CHECK(fixture.value(cutoff) == doctest::Approx(0.25));
  }
}

TEST_CASE("Значения после сохранения и загрузки совпадают точно") {
  ParamsFixture fixture;
  std::mt19937 random(11);
  std::uniform_real_distribution<double> position(0.0, 1.0);

  fixture.start();

  std::vector<clap_event_param_value_t> values;
  std::vector<const clap_event_header_t*> events;

  values.reserve(SLOT_COUNT);

  for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
    values.push_back(makeValue(slot, position(random)));
    events.push_back(&values.back().header);
  }

  fixture.process(256, events);

  const std::string saved = fixture.save();
  ParamsFixture restored;

  REQUIRE(restored.load(saved));

  for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
    CAPTURE(slot);
    CHECK(restored.value(slot) == fixture.value(slot));
  }

  CHECK(restored.save() == saved);
}

TEST_CASE("Polyphony идёт через модель: лимит приходит с планом (D-053)") {
  ParamsFixture fixture;
  const clap_id polyphony = fixture.findSlot("G1 Polyphony");

  fixture.start();

  const clap_event_note_t first = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
  const clap_event_note_t second = makeNote(CLAP_EVENT_NOTE_ON, KEY_C5);
  const clap_event_param_value_t single = makeValue(polyphony, 0.0);

  // Значение пришло, но лимит изменится только с планом: это делает главный поток
  fixture.process(256, { &first.header, &second.header, &single.header });

  CHECK(fixture.callbacks > 0);

  const float both = peak(fixture.process(4800));

  fixture.mainThread();
  fixture.process(SMOOTHING_FRAMES * 3);
  fixture.mainThread();

  CHECK(fixture.value(polyphony) == 0.0);
  CHECK(peak(fixture.process(4800)) < both * 0.75f);
}

TEST_CASE("Polyphony выше прошлого лимита работает: память голосов выделяется на главном потоке") {
  ParamsFixture fixture;
  const clap_id polyphony = fixture.findSlot("G1 Polyphony");

  fixture.start();

  const clap_event_param_value_t most = makeValue(polyphony, 1.0);

  fixture.process(256, { &most.header });
  fixture.mainThread();

  for (int16_t key = 40; key < 104; key++) {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, key);

    fixture.process(32, { &on.header });
  }

  fixture.mainThread();

  CHECK(peak(fixture.process(256)) > 0.0f);
}

TEST_CASE("Flush на неактивном плагине применяет значение сразу, без звука") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

  const clap_event_param_value_t value = makeValue(cutoff, 0.0);
  const clap_event_note_t note = makeNote(CLAP_EVENT_NOTE_ON, 60);

  // Нота в flush не относится к параметрам и не должна запустить голос
  fixture.flush({ &value.header, &note.header });

  CHECK(fixture.value(cutoff) == 0.0);
  CHECK(fixture.callbacks == 0);

  // Значение принято сразу: первая нота звучит уже с ним, без сглаживания от старого
  fixture.start();

  CHECK(peak(fixture.process(256)) == 0.0f);

  const std::vector<float> quiet = fixture.process(256, { &note.header });

  ParamsFixture reference;
  ModuleRegistry registry;

  REQUIRE(addAllModules(registry).has_value());

  const auto model = loadModel(&registry, fixture.save());

  REQUIRE(model.has_value());
  REQUIRE(reference.load(saveModel(*model)));
  reference.start();
  reference.process(256);

  CHECK(reference.process(256, { &note.header }) == quiet);
}

TEST_CASE("Flush на работающем плагине просит главный поток дойти до модели") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

  fixture.start();
  fixture.process(256);

  const int before = fixture.callbacks;
  const clap_event_param_value_t value = makeValue(cutoff, 0.5);

  fixture.flush({ &value.header });

  CHECK(fixture.callbacks > before);
  CHECK(fixture.value(cutoff) == doctest::Approx(0.5));

  // Flush только про параметры: нота из него не должна запустить голос
  const clap_event_note_t note = makeNote(CLAP_EVENT_NOTE_ON, 60);

  fixture.flush({ &note.header });

  CHECK(peak(fixture.process(256)) == 0.0f);
}

TEST_CASE("Плагин не падает и не меняет значения от неверных событий") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");
  const double before = fixture.value(cutoff);

  fixture.start();

  SUBCASE("Чужое пространство событий") {
    clap_event_param_value_t value = makeValue(cutoff, 0.1);

    value.header.space_id = (uint16_t)(CLAP_CORE_EVENT_SPACE_ID + 1);
    fixture.process(256, { &value.header });
    CHECK(fixture.value(cutoff) == before);
  }

  SUBCASE("Не число не доходит ни до модели, ни до звука") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, 60);
    const clap_event_param_value_t value = makeValue(cutoff, std::numeric_limits<double>::quiet_NaN());

    fixture.process(256, { &on.header, &value.header });

    CHECK(fixture.value(cutoff) == before);
    CHECK(std::isfinite(peak(fixture.process(SMOOTHING_FRAMES * 2))));
    CHECK(peak(fixture.process(256)) > 0.0f);
  }

  SUBCASE("Слот вне списка") {
    const clap_event_param_value_t value = makeValue(SLOT_COUNT, 0.1);

    fixture.process(256, { &value.header });
    CHECK(fixture.value(cutoff) == before);
  }

  SUBCASE("Значение вне диапазона ограничивается") {
    const clap_event_param_value_t value = makeValue(cutoff, 7.0);

    fixture.process(256, { &value.header });
    CHECK(fixture.value(cutoff) == 1.0);
  }

  SUBCASE("Скрытый слот ничего не меняет, значение остаётся 0") {
    const clap_event_param_value_t value = makeValue(SLOT_COUNT - 1, 0.75);

    fixture.process(256, { &value.header });
    CHECK(fixture.value(SLOT_COUNT - 1) == 0.0);
    CHECK(fixture.value(cutoff) == before);
  }
}

TEST_CASE("Макрос: значение хоста через матрицу управляет громкостью (D-011, D-055)") {
  ParamsFixture fixture;

  REQUIRE(fixture.load(buildMacroPreset()));
  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);

  CHECK(peak(fixture.process(4800, { &on.header })) == 0.0f);

  const clap_event_param_value_t half = makeValue(0, 0.5);

  fixture.process(SMOOTHING_FRAMES * 2, { &half.header });

  const float level = peak(fixture.process(4800));

  CHECK(level > 0.01f);

  const clap_event_param_value_t full = makeValue(0, 1.0);

  fixture.process(SMOOTHING_FRAMES * 2, { &full.header });

  CHECK(peak(fixture.process(4800)) == doctest::Approx(level * 2.0f).epsilon(0.01));

  // Положение макроса относится к звуку и сохраняется в пресете
  ParamsFixture restored;

  REQUIRE(restored.load(fixture.save()));
  CHECK(restored.value(0) == 1.0);
}

TEST_CASE("Затихающий сигнал не даёт субнормальных чисел: они дороги для процессора (D-062)") {
  ParamsFixture fixture;

  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
  const clap_event_param_value_t silence = makeValue(fixture.findSlot("G1 Oscillator 1 Level"), 0.0);

  fixture.process(4800, { &on.header });

  // Нота звучит, вход фильтра затих: его состояние убывает по экспоненте. Субнормальным оно
  // становится на короткое время сразу после конца сглаживания, поэтому важен каждый сэмпл
  uint32_t subnormal = 0;
  std::vector<const clap_event_header_t*> events = { &silence.header };

  for (int block = 0; block < 100; block++) {
    for (const float sample : fixture.process(512, events)) {
      subnormal += std::fpclassify(sample) == FP_SUBNORMAL ? 1 : 0;
    }

    events.clear();
  }

  CHECK(subnormal == 0);
}

TEST_CASE("Режим обработки малых чисел после process возвращается как был: поток принадлежит хосту (D-062)") {
  ParamsFixture fixture;

  fixture.start();

  // Единственный заметный снаружи признак режима: субнормальное число остаётся собой
  volatile float tiny = std::numeric_limits<float>::min();

  fixture.process(256);

  const float half = tiny * 0.5f;

  CHECK(std::fpclassify(half) == FP_SUBNORMAL);
}

TEST_CASE("Значение вне 0..1 ограничивается до модуляции: иначе база перекрыла бы маршрут (D-025)") {
  ModuleRegistry registry;
  REQUIRE(addAllModules(registry).has_value());

  auto model = loadModel(&registry, buildMacroPreset());

  REQUIRE(model.has_value());

  // Макрос теперь убирает громкость: база 1 минус макрос 1 даёт тишину
  ModuleId amp = 0;

  for (const auto& [id, module] : model->getModules()) {
    amp = module.descriptor->type == "amp" ? id : amp;
  }

  REQUIRE(model->apply(SetRouteDepth{ { MACROS_MODULE_ID, "macro1" }, { amp, "gain" }, -1.0f }).has_value());

  ParamsFixture fixture;

  REQUIRE(fixture.load(saveModel(*model)));
  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
  const clap_event_param_value_t macro = makeValue(0, 1.0);
  const clap_event_param_value_t gain = makeValue(fixture.findSlot("G1 Amp 1 Gain"), 7.0);

  fixture.process(SMOOTHING_FRAMES * 3, { &on.header, &macro.header, &gain.header });

  CHECK(peak(fixture.process(4800)) == 0.0f);
}

TEST_CASE("Аудиопоток не выделяет память на значениях от хоста (D-023)") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");
  const clap_id polyphony = fixture.findSlot("G1 Polyphony");

  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, 60);
  std::vector<clap_event_param_value_t> values = {
    makeValue(cutoff, 0.1),
    makeValue(polyphony, 0.5),
    makeValue(0, 0.5),
    makeValue(SLOT_COUNT - 1, 0.5),
  };

  fixture.process(256, { &on.header });

  std::vector<float> left(256);
  std::vector<float> right(256);
  float* channels[2] = { left.data(), right.data() };
  std::vector<const clap_event_header_t*> events;

  for (const clap_event_param_value_t& value : values) {
    events.push_back(&value.header);
  }

  clap_audio_buffer_t output{};
  output.data32 = channels;
  output.channel_count = 2;

  const clap_input_events_t input = {
    .ctx = &events,
    .size = [](const clap_input_events_t* list) {
      return (uint32_t)((std::vector<const clap_event_header_t*>*)list->ctx)->size();
    },
    .get = [](const clap_input_events_t* list, uint32_t index) {
      return (*(std::vector<const clap_event_header_t*>*)list->ctx)[index];
    },
  };

  clap_process_t process{};
  process.frames_count = 256;
  process.audio_outputs = &output;
  process.audio_outputs_count = 1;
  process.in_events = &input;

  const size_t before = getThreadMemoryOperations();

  for (int block = 0; block < 100; block++) {
    fixture.plugin->process(fixture.plugin, &process);
  }

  CHECK(getThreadMemoryOperations() == before);
}

TEST_CASE("Загрузка состояния сообщает хосту, что изменилось (D-011)") {
  ParamsFixture fixture;

  SUBCASE("Другой пресет: новые имена и значения, занятые другим параметром слоты сброшены") {
    const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

    REQUIRE(fixture.load(buildTwoOscillatorPreset()));

    CHECK(fixture.rescans == std::vector<clap_param_rescan_flags>{
      CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_INFO,
    });
    CHECK(std::ranges::contains(fixture.cleared, cutoff));

    // Макросы постоянны: их автоматизация остаётся
    CHECK_FALSE(std::ranges::contains(fixture.cleared, (clap_id)0));
  }

  SUBCASE("То же состояние не трогает хост") {
    REQUIRE(fixture.load(fixture.save()));

    CHECK(fixture.rescans.empty());
    CHECK(fixture.cleared.empty());
  }

  SUBCASE("Те же слоты с другими значениями: сброса нет, но значения по умолчанию новые") {
    ParamsFixture other;
    const clap_event_param_value_t value = makeValue(other.findSlot("G1 Filter 1 Cutoff"), 0.5);

    other.flush({ &value.header });

    REQUIRE(fixture.load(other.save()));

    CHECK(fixture.rescans == std::vector<clap_param_rescan_flags>{
      CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_INFO,
    });
    CHECK(fixture.cleared.empty());
  }

  SUBCASE("Отвергнутое состояние ничего не меняет") {
    const std::string saved = fixture.save();

    CHECK_FALSE(fixture.load("not a state"));
    CHECK(fixture.save() == saved);
    CHECK(fixture.rescans.empty());
  }
}

TEST_CASE("Хост без расширений: плагин работает, просто не докладывает") {
  ParamsFixture fixture(false);

  REQUIRE(fixture.load(buildTwoOscillatorPreset()));

  CHECK(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, nullptr) == false);
  CHECK(fixture.value(0) == 0.0);
}

TEST_CASE("Таблица слотов переживает сохранение: автоматизация остаётся на том же параметре (D-011)") {
  ModuleRegistry registry;
  REQUIRE(addAllModules(registry).has_value());

  ParamsFixture source;
  json file = json::parse(source.save());

  // В проекте параметр мог занимать любой слот: например, раньше перед ним стоял удалённый модуль
  const clap_id original = source.findSlot("G1 Filter 1 Cutoff");
  const clap_id moved = 500;

  for (json& entry : file.at("session").at("slots")) {
    if (entry.at("slot") == original) {
      entry["slot"] = moved;
    }
  }

  const clap_event_param_value_t value = makeValue(moved, 0.25);
  ParamsFixture restored;

  REQUIRE(restored.load(file.dump()));
  CHECK(restored.findSlot("G1 Filter 1 Cutoff") == moved);

  restored.start();
  restored.process(256, { &value.header });

  CHECK(restored.value(moved) == doctest::Approx(0.25));

  const json saved = json::parse(restored.save());
  const auto& slots = saved.at("session").at("slots");

  CHECK(std::ranges::any_of(slots, [&](const json& entry) {
    return entry.at("slot") == moved && entry.at("parameter") == "cutoff";
  }));
}

TEST_CASE("Загруженная таблица сразу работает в звуке: адреса слотов приходят с планом") {
  ParamsFixture fixture;

  fixture.start();
  REQUIRE(fixture.load(buildMacroPreset()));

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
  const clap_event_param_value_t full = makeValue(0, 1.0);

  fixture.process(256);
  fixture.mainThread();
  fixture.process(SMOOTHING_FRAMES * 2, { &on.header, &full.header });

  CHECK(peak(fixture.process(4800)) > 0.01f);
}

TEST_CASE("После смены пресета слоты ведут к его параметрам: адреса приходят с планом") {
  ParamsFixture fixture;

  fixture.start();
  REQUIRE(fixture.load(buildTwoOscillatorPreset()));
  fixture.process(256);
  fixture.mainThread();

  const clap_id level = fixture.findSlot("G1 Oscillator 2 Level");
  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);

  fixture.process(SMOOTHING_FRAMES * 2, { &on.header });

  const float before = peak(fixture.process(4800));
  const clap_event_param_value_t full = makeValue(level, 1.0);

  fixture.process(SMOOTHING_FRAMES * 2, { &full.header });

  CHECK(peak(fixture.process(4800)) > before * 2.0f);
}

TEST_CASE("Значения, не дошедшие до модели, не попадают в новое состояние") {
  ParamsFixture fixture;
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");

  fixture.start();

  ParamsFixture other;
  const clap_event_param_value_t otherValue = makeValue(cutoff, 0.5);

  other.flush({ &otherValue.header });

  const clap_event_param_value_t value = makeValue(cutoff, 0.1);

  fixture.process(256, { &value.header });
  REQUIRE(fixture.load(other.save()));
  fixture.mainThread();

  CHECK(fixture.value(cutoff) == doctest::Approx(0.5));
}

TEST_CASE("Загрузка пресета из файла: звук, имя и слоты (ТЗ §9.2, D-059)") {
  ParamsFixture fixture;
  const std::string path = writeTemporary("sint-test-Мой пресет.sintpreset", buildMacroPreset());

  REQUIRE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));

  CHECK(fixture.loadedPresets == std::vector<std::string>{ path });
  CHECK(fixture.presetErrors.empty());
  CHECK(fixture.rescans.size() == 1);

  const json saved = json::parse(fixture.save());

  CHECK(saved.at("session").at("presetName") == "sint-test-Мой пресет");

  // Новые параметры сразу видны хосту
  CHECK(std::ranges::any_of(saved.at("session").at("slots"), [](const json& entry) {
    return entry.at("type") == "oscillator";
  }));
  CHECK(fixture.findSlot("G1 Amp 1 Gain") > 0);

  std::filesystem::remove(path);
}

TEST_CASE("Пресет сохраняет слот модуля с тем же ID и типом: автоматизация не теряется") {
  ParamsFixture fixture;
  const clap_id polyphony = fixture.findSlot("G1 Polyphony");
  const std::string path = writeTemporary("sint-test-keep.sintpreset", buildTwoOscillatorPreset());

  REQUIRE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));

  CHECK(fixture.findSlot("G1 Polyphony") == polyphony);
  CHECK_FALSE(std::ranges::contains(fixture.cleared, polyphony));

  std::filesystem::remove(path);
}

TEST_CASE("Пресет не трогает номер слота, который не совпадает с порядком выдачи") {
  ParamsFixture fixture;
  json file = json::parse(fixture.save());
  const clap_id original = fixture.findSlot("G1 Polyphony");
  const clap_id moved = 500;

  for (json& entry : file.at("session").at("slots")) {
    if (entry.at("slot") == original) {
      entry["slot"] = moved;
    }
  }

  REQUIRE(fixture.load(file.dump()));
  REQUIRE(fixture.findSlot("G1 Polyphony") == moved);

  const std::string path = writeTemporary("sint-test-moved.sintpreset", buildTwoOscillatorPreset());

  REQUIRE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));

  CHECK(fixture.findSlot("G1 Polyphony") == moved);
  CHECK(fixture.findSlot("G1 Oscillator 2 Level") > 0);

  std::filesystem::remove(path);
}

TEST_CASE("Ошибка загрузки пресета: состояние не меняется, хост узнаёт причину") {
  ParamsFixture fixture;
  const std::string saved = fixture.save();

  SUBCASE("Файла нет") {
    CHECK_FALSE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/нет/такого.sintpreset", nullptr));
  }

  SUBCASE("Файл испорчен") {
    const std::string path = writeTemporary("sint-test-broken.sintpreset", "not a preset");

    CHECK_FALSE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));

    std::filesystem::remove(path);
  }

  SUBCASE("Не файл, даже если путь настоящий") {
    const std::string path = writeTemporary("sint-test-kind.sintpreset", buildMacroPreset());

    CHECK_FALSE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, path.c_str(), nullptr));

    std::filesystem::remove(path);
  }

  SUBCASE("Нет пути") {
    CHECK_FALSE(fixture.presets->from_location(fixture.plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, nullptr, nullptr));
  }

  CHECK(fixture.presetErrors.size() == 1);
  CHECK(fixture.loadedPresets.empty());
  CHECK(fixture.save() == saved);
}

TEST_CASE("Загрузка пресетов доступна и под совместимым ID расширения") {
  ParamsFixture fixture;

  CHECK(fixture.plugin->get_extension(fixture.plugin, CLAP_EXT_PRESET_LOAD_COMPAT) == fixture.presets);
}

TEST_CASE("Автоматизация во время правок главного потока (стресс-тест, запускать и под ThreadSanitizer)") {
  ParamsFixture fixture;
  const std::string first = fixture.save();
  const std::string second = buildTwoOscillatorPreset();
  const clap_id cutoff = fixture.findSlot("G1 Filter 1 Cutoff");
  const clap_id polyphony = fixture.findSlot("G1 Polyphony");

  fixture.start();

  std::atomic<bool> isRunning = true;
  uint64_t blocks = 0;
  uint64_t badSamples = 0;
  size_t memoryOperations = 0;

  std::thread audioThread([&] {
    std::mt19937 random(5);
    std::vector<float> left(128);
    std::vector<float> right(128);
    float* channels[2] = { left.data(), right.data() };
    std::array<clap_event_param_value_t, 4> values;
    std::array<const clap_event_header_t*, 5> events;
    clap_event_note_t note = makeNote(CLAP_EVENT_NOTE_ON, 60);

    const clap_input_events_t input = {
      .ctx = &events,
      .size = [](const clap_input_events_t* list) {
        return (uint32_t)((std::array<const clap_event_header_t*, 5>*)list->ctx)->size();
      },
      .get = [](const clap_input_events_t* list, uint32_t index) {
        return (*(std::array<const clap_event_header_t*, 5>*)list->ctx)[index];
      },
    };

    clap_audio_buffer_t output{};
    output.data32 = channels;
    output.channel_count = 2;

    clap_process_t process{};
    process.frames_count = 128;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &input;

    const size_t before = getThreadMemoryOperations();

    while (isRunning) {
      const auto position = (double)(random() % 1000) / 1000.0;

      values[0] = makeValue(cutoff, position);
      values[1] = makeValue(polyphony, position);
      values[2] = makeValue(0, position);
      values[3] = makeValue(random() % SLOT_COUNT, position);

      for (size_t index = 0; index < values.size(); index++) {
        events[index] = &values[index].header;
      }

      note.header.type = random() % 3 == 0 ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
      note.key = (int16_t)(48 + random() % 24);
      events[4] = &note.header;

      fixture.plugin->process(fixture.plugin, &process);

      blocks++;
      badSamples += (uint64_t)std::ranges::count_if(left, [](float sample) {
        return !std::isfinite(sample);
      });
    }

    memoryOperations = getThreadMemoryOperations() - before;
  });

  double sum = 0.0;

  for (int step = 0; step < 3000; step++) {
    fixture.mainThread();

    if (step % 3 == 0) {
      double value = 0.0;

      fixture.params->get_value(fixture.plugin, (uint32_t)step % SLOT_COUNT, &value);
      sum += value;
    }

    if (step % 200 == 0) {
      REQUIRE(json::accept(fixture.save()));
    }

    if (step % 500 == 250) {
      REQUIRE(fixture.load(step % 1000 == 250 ? second : first));
    }

    if (step % 4 == 0) {
      std::this_thread::yield();
    }
  }

  isRunning = false;
  audioThread.join();

  CHECK(blocks > 0);
  CHECK(badSamples == 0);
  CHECK(memoryOperations == 0);
  CHECK(std::isfinite(sum));
}
