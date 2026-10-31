#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../clap/clap.h"
#include "../engine/filter/filterProcessor.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/oscillator/oscillatorProcessor.h"
#include "../engine/preset/preset.h"
#include "../engine/synthModel/synthModel.h"
#include "../tools/offlineHost/offlineHost.h"

namespace {
  constexpr uint32_t SAMPLE_RATE = 48000;

  // Быстрое затухание голоса: 10 мс
  constexpr uint32_t FADE_FRAMES = 10 * SAMPLE_RATE / 1000;
  constexpr uint32_t CHUNK_FRAMES = 32;

  // Release огибающей пресета по умолчанию: 200 мс
  constexpr uint32_t RELEASE_FRAMES = 200 * SAMPLE_RATE / 1000;

  // Нота A4, 440 Гц
  constexpr int16_t KEY_A4 = 69;

  const clap_plugin_factory_t* getFactory() {
    return (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  }

  bool isSilent(const std::vector<float>& channel, uint32_t from, uint32_t to) {
    for (uint32_t i = from; i < to; i++) {
      if (channel[i] != 0.0f) {
        return false;
      }
    }

    return true;
  }

  float peak(const std::vector<float>& channel, uint32_t from, uint32_t to) {
    float result = 0.0f;

    for (uint32_t i = from; i < to; i++) {
      result = std::fmax(result, std::fabs(channel[i]));
    }

    return result;
  }

  int countRisingCrossings(const std::vector<float>& channel) {
    int crossings = 0;

    for (size_t i = 1; i < channel.size(); i++) {
      if (channel[i - 1] < 0.0f && channel[i] >= 0.0f) {
        crossings++;
      }
    }

    return crossings;
  }

  /**
   * Пресет: в генераторе синус идёт через фильтр, сумма голосов идёт на выход
   */
  std::string buildPreset(float cutoff = 20000.0f, bool isFilterEnabled = true) {
    ModuleRegistry registry;
    REQUIRE(addAllModules(registry).has_value());

    SynthModel model(&registry);
    const ModuleId generator = **model.apply(AddModule{ "generator" });
    const ModuleId osc = **model.apply(AddModule{ "oscillator", generator });
    const ModuleId filter = **model.apply(AddModule{ "filter", generator });
    ModuleId voiceOutput = 0;

    for (const auto& [id, module] : model.getModules()) {
      if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
        voiceOutput = id;
      }
    }

    for (const Command& command : std::initializer_list<Command>{
      SetParameter{ osc, "wave", 1.0f },
      SetParameter{ osc, "level", 0.5f },
      SetParameter{ filter, "cutoff", cutoff },
      SetModuleEnabled{ filter, isFilterEnabled },
      Connect{ { osc, "out", filter, "in" } },
      Connect{ { filter, "out", voiceOutput, "in" } },
      Connect{ { generator, "out", model.getOutputId(), "in" } },
    }) {
      REQUIRE(model.apply(command).has_value());
    }

    return saveModel(model);
  }

  /**
   * Состояние проекта без раздела session: то, что совпадает с пресетом (D-020)
   */
  std::string withoutSession(const std::string& state) {
    nlohmann::json file = nlohmann::json::parse(state);

    file.erase("session");

    return file.dump(2);
  }

  /**
   * Нота, которую не отпускают до конца рендера
   */
  std::vector<OfflineNote> heldNote(int16_t key = KEY_A4) {
    return { { .key = key, .startFrame = 0, .endFrame = UINT32_MAX } };
  }

  clap_event_note_t makeNote(uint16_t type, int16_t key, double velocity = 1.0, int32_t noteId = -1) {
    clap_event_note_t event{};
    event.header.size = sizeof(clap_event_note_t);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = type;
    event.note_id = noteId;
    event.key = key;
    event.velocity = velocity;

    return event;
  }

  clap_event_midi_t makeMidi(uint8_t status, uint8_t first, uint8_t second) {
    clap_event_midi_t event{};
    event.header.size = sizeof(clap_event_midi_t);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.data[0] = status;
    event.data[1] = first;
    event.data[2] = second;

    return event;
  }

  /**
   * Поток чтения, отдающий данные мелкими порциями: CLAP этого не запрещает
   */
  struct ChunkedReader {
    std::string text;
    size_t chunkSize;
    bool shouldFail = false;
    size_t offset = 0;
    clap_istream_t stream = { .ctx = this, .read = ChunkedReader::read };

    static int64_t read(const clap_istream_t* source, void* buffer, uint64_t size) {
      auto* reader = (ChunkedReader*)source->ctx;

      if (reader->shouldFail) {
        return -1;
      }

      const size_t count = std::min({ (size_t)size, reader->chunkSize, reader->text.size() - reader->offset });

      std::memcpy(buffer, reader->text.data() + reader->offset, count);
      reader->offset += count;

      return (int64_t)count;
    }
  };

  /**
   * Поток записи, принимающий данные мелкими порциями
   */
  struct ChunkedWriter {
    size_t chunkSize;
    bool shouldFail = false;
    std::string text = {};
    clap_ostream_t stream = { .ctx = this, .write = ChunkedWriter::write };

    static int64_t write(const clap_ostream_t* target, const void* buffer, uint64_t size) {
      auto* writer = (ChunkedWriter*)target->ctx;

      if (writer->shouldFail) {
        return -1;
      }

      const size_t count = std::min((size_t)size, writer->chunkSize);

      writer->text.append((const char*)buffer, count);

      return (int64_t)count;
    }
  };

  /**
   * Инициализированный плагин без рендера, для проверки состояния
   */
  struct PluginFixture {
    clap_host_t host{};
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_state_t* state = nullptr;

    PluginFixture() {
      const clap_plugin_factory_t* factory = getFactory();

      this->host.clap_version = CLAP_VERSION;
      this->host.get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
      this->host.request_callback = [](const clap_host_t*) {};
      this->plugin = factory->create_plugin(factory, &this->host, factory->get_plugin_descriptor(factory, 0)->id);

      REQUIRE(this->plugin != nullptr);
      REQUIRE(this->plugin->init(this->plugin));

      this->state = (const clap_plugin_state_t*)this->plugin->get_extension(this->plugin, CLAP_EXT_STATE);

      REQUIRE(this->state != nullptr);
    }

    ~PluginFixture() {
      this->plugin->destroy(this->plugin);
    }

    bool load(const std::string& text, size_t chunkSize = 4096) {
      ChunkedReader reader = { .text = text, .chunkSize = chunkSize };

      return this->state->load(this->plugin, &reader.stream);
    }

    std::string save() {
      ChunkedWriter writer = { .chunkSize = 4096 };

      REQUIRE(this->state->save(this->plugin, &writer.stream));

      return writer.text;
    }

    void start() {
      REQUIRE(this->plugin->activate(this->plugin, SAMPLE_RATE, 1, 8192));
      REQUIRE(this->plugin->start_processing(this->plugin));
    }

    /**
     * Обрабатывает блок с заданными событиями и возвращает левый канал
     */
    std::vector<float> process(uint32_t frames, std::vector<const clap_event_header_t*> events = {}) {
      std::vector<float> left(frames);
      std::vector<float> right(frames);
      float* channels[2] = { left.data(), right.data() };

      clap_audio_buffer_t output{};
      output.data32 = channels;
      output.channel_count = 2;

      const clap_input_events_t inEvents = {
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
      process.in_events = &inEvents;

      REQUIRE(this->plugin->process(this->plugin, &process) != CLAP_PROCESS_ERROR);

      return left;
    }
  };
}

TEST_CASE("Дескриптор содержит постоянный ID и теги инструмента") {
  const clap_plugin_factory_t* factory = getFactory();
  REQUIRE(factory != nullptr);
  REQUIRE(factory->get_plugin_count(factory) == 1);

  const clap_plugin_descriptor_t* descriptor = factory->get_plugin_descriptor(factory, 0);
  REQUIRE(descriptor != nullptr);

  // Менять нельзя: DAW находит плагин в проектах по этой строке (D-022)
  CHECK(std::string(descriptor->id) == "com.chillyswim.sin-t");

  std::vector<std::string> features;
  for (const char* const* feature = descriptor->features; *feature; feature++) {
    features.emplace_back(*feature);
  }

  CHECK(features == std::vector<std::string>{
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
  });
}

TEST_CASE("Порты: один стерео-выход и один нотный вход") {
  const clap_plugin_factory_t* factory = getFactory();
  clap_host_t host{};
  host.clap_version = CLAP_VERSION;

  const clap_plugin_t* plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
  REQUIRE(plugin != nullptr);

  const auto* audioPorts = (const clap_plugin_audio_ports_t*)plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
  const auto* notePorts = (const clap_plugin_note_ports_t*)plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS);
  REQUIRE(audioPorts != nullptr);
  REQUIRE(notePorts != nullptr);

  CHECK(audioPorts->count(plugin, true) == 0);
  CHECK(audioPorts->count(plugin, false) == 1);
  CHECK(notePorts->count(plugin, true) == 1);
  CHECK(notePorts->count(plugin, false) == 0);

  clap_audio_port_info_t audioInfo{};
  CHECK_FALSE(audioPorts->get(plugin, 0, true, &audioInfo));
  CHECK_FALSE(audioPorts->get(plugin, 1, false, &audioInfo));
  REQUIRE(audioPorts->get(plugin, 0, false, &audioInfo));
  CHECK(audioInfo.channel_count == 2);
  CHECK(std::string(audioInfo.port_type) == CLAP_PORT_STEREO);

  clap_note_port_info_t noteInfo{};
  CHECK_FALSE(notePorts->get(plugin, 0, false, &noteInfo));
  CHECK_FALSE(notePorts->get(plugin, 1, true, &noteInfo));
  REQUIRE(notePorts->get(plugin, 0, true, &noteInfo));

  // Объявлены только те диалекты, которые плагин действительно обрабатывает
  CHECK(noteInfo.supported_dialects == (CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI));
  CHECK(noteInfo.preferred_dialect == CLAP_NOTE_DIALECT_CLAP);

  plugin->destroy(plugin);
}

TEST_CASE("Нота звучит с точностью до сэмпла и затихает после release (D-051)") {
  const uint32_t startFrame = 1000;
  const uint32_t endFrame = 24000;
  const uint32_t silenceFrame = endFrame + RELEASE_FRAMES + FADE_FRAMES + 2 * CHUNK_FRAMES;

  const StereoBuffer audio = renderOffline(
    { { .key = 60, .startFrame = startFrame, .endFrame = endFrame } },
    { .frameCount = silenceFrame + 2000, .sampleRate = SAMPLE_RATE }
  );

  REQUIRE(audio.left.size() == silenceFrame + 2000);

  // Пила начинается с середины скачка, поэтому самый первый сэмпл ноты равен нулю
  CHECK(isSilent(audio.left, 0, startFrame + 1));
  CHECK(audio.left[startFrame + 1] != 0.0f);
  CHECK(peak(audio.left, startFrame, endFrame) > 0.01f);
  CHECK(peak(audio.left, startFrame, endFrame) < 1.0f);
  CHECK(peak(audio.left, endFrame + RELEASE_FRAMES - FADE_FRAMES, endFrame + RELEASE_FRAMES) > 0.0f);
  CHECK(isSilent(audio.left, silenceFrame, (uint32_t)audio.left.size()));
  CHECK(audio.left == audio.right);
}

TEST_CASE("Рендер одинаков при повторе и при любом размере блока хоста") {
  const std::vector<OfflineNote> notes = {
    { .key = 60, .startFrame = 100, .endFrame = 20000 },
    { .key = 64, .startFrame = 5003, .endFrame = 15000 },
  };
  const uint32_t frameCount = 30000;

  const StereoBuffer reference = renderOffline(notes, { .frameCount = frameCount, .blockSize = 256 });
  REQUIRE(reference.left.size() == frameCount);

  CHECK(renderOffline(notes, { .frameCount = frameCount, .blockSize = 256 }).left == reference.left);

  for (const uint32_t blockSize : { 1u, 37u, 64u, 1024u, 8192u }) {
    CAPTURE(blockSize);
    CHECK(renderOffline(notes, { .frameCount = frameCount, .blockSize = blockSize }).left == reference.left);
  }
}

TEST_CASE("Загруженный граф звучит через CLAP (D-032)") {
  const StereoBuffer audio = renderOffline(heldNote(), {
    .frameCount = SAMPLE_RATE,
    .sampleRate = SAMPLE_RATE,
    .preset = buildPreset(),
  });

  REQUIRE(audio.left.size() == SAMPLE_RATE);

  CHECK(std::abs(countRisingCrossings(audio.left) - 440) <= 1);
  CHECK(peak(audio.left, 0, SAMPLE_RATE) > 0.3f);
  CHECK(audio.left == audio.right);
}

TEST_CASE("Без нот граф молчит") {
  const StereoBuffer audio = renderOffline({}, { .frameCount = 4800, .preset = buildPreset() });

  REQUIRE(audio.left.size() == 4800);
  CHECK(isSilent(audio.left, 0, 4800));
}

TEST_CASE("Отключённый модуль из пресета пропускает сигнал через CLAP (D-040)") {
  // Высокая нота: фильтр с низким срезом подавляет её почти полностью
  const int16_t key = 111;

  const StereoBuffer filtered = renderOffline(heldNote(key), {
    .frameCount = 4800,
    .sampleRate = SAMPLE_RATE,
    .preset = buildPreset(20.0f),
  });
  const StereoBuffer bypassed = renderOffline(heldNote(key), {
    .frameCount = 4800,
    .sampleRate = SAMPLE_RATE,
    .preset = buildPreset(20.0f, false),
  });

  CHECK(peak(filtered.left, 0, 4800) < 0.05f);
  CHECK(peak(bypassed.left, 0, 4800) > 0.3f);
}

TEST_CASE("Частота дискретизации хоста доходит до модулей подграфа") {
  const uint32_t sampleRate = 96000;

  const StereoBuffer audio = renderOffline(heldNote(), {
    .frameCount = sampleRate,
    .sampleRate = sampleRate,
    .preset = buildPreset(),
  });

  REQUIRE(audio.left.size() == sampleRate);

  CHECK(std::abs(countRisingCrossings(audio.left) - 440) <= 1);
}

TEST_CASE("Звук графа через CLAP не зависит от размера блока") {
  const std::string preset = buildPreset();
  const std::vector<OfflineNote> notes = {
    { .key = 60, .startFrame = 100, .endFrame = 5000 },
    { .key = 64, .startFrame = 2003, .endFrame = 7001 },
  };
  const uint32_t frameCount = 9000;

  const StereoBuffer reference = renderOffline(notes, { .frameCount = frameCount, .blockSize = 256, .preset = preset });
  REQUIRE(reference.left.size() == frameCount);

  for (const uint32_t blockSize : { 1u, 37u, 1024u }) {
    CAPTURE(blockSize);
    CHECK(renderOffline(notes, { .frameCount = frameCount, .blockSize = blockSize, .preset = preset }).left == reference.left);
  }
}

TEST_CASE("Плагин сразу играет из пресета по умолчанию (ТЗ §9.1)") {
  const StereoBuffer audio = renderOffline(
    { { .key = 60, .startFrame = 0, .endFrame = 4800 } },
    { .frameCount = 4800 }
  );

  REQUIRE(audio.left.size() == 4800);
  CHECK(peak(audio.left, 0, 4800) > 0.01f);

  // Сохранённый проект с настройками по умолчанию открывается и звучит так же
  PluginFixture fixture;
  const std::string initial = fixture.save();

  CHECK(renderOffline(
    { { .key = 60, .startFrame = 0, .endFrame = 4800 } },
    { .frameCount = 4800, .preset = initial }
  ).left == audio.left);
}

TEST_CASE("Повторная активация начинается без голосов прошлого запуска") {
  PluginFixture fixture;

  REQUIRE(fixture.load(buildPreset()));
  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);

  fixture.process(100, { &on.header });

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
  fixture.start();

  CHECK(isSilent(fixture.process(1000), 0, 1000));

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

TEST_CASE("Ноты в виде сырого MIDI") {
  PluginFixture fixture;

  REQUIRE(fixture.load(buildPreset()));
  fixture.start();

  SUBCASE("Note on запускает голос, note off его отпускает") {
    const clap_event_midi_t on = makeMidi(0x90, KEY_A4, 127);
    const clap_event_midi_t off = makeMidi(0x80, KEY_A4, 0);

    CHECK(peak(fixture.process(4800, { &on.header }), 0, 4800) > 0.3f);

    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Note on с нулевой velocity в MIDI 1 означает отпускание") {
    const clap_event_midi_t on = makeMidi(0x90, KEY_A4, 127);
    const clap_event_midi_t off = makeMidi(0x90, KEY_A4, 0);

    fixture.process(100, { &on.header });
    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

TEST_CASE("Педаль sustain и команды паники через MIDI (D-058)") {
  PluginFixture fixture;

  REQUIRE(fixture.load(buildPreset()));
  fixture.start();

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
  const clap_event_note_t off = makeNote(CLAP_EVENT_NOTE_OFF, KEY_A4);
  const clap_event_midi_t pedalDown = makeMidi(0xB0, 64, 127);
  const clap_event_midi_t pedalUp = makeMidi(0xB0, 64, 0);
  const uint32_t fadeFrames = FADE_FRAMES + CHUNK_FRAMES;

  fixture.process(100, { &on.header });

  SUBCASE("Без педали нота затихает сразу") {
    fixture.process(fadeFrames, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Педаль держит отпущенную ноту, отпускание педали её заканчивает") {
    // Педаль приходит сырым MIDI, а нота событием CLAP: так делает Reaper
    fixture.process(100, { &pedalDown.header });
    fixture.process(100, { &off.header });

    CHECK(peak(fixture.process(4800), 0, 4800) > 0.3f);

    fixture.process(fadeFrames, { &pedalUp.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Значение ниже 64 педаль не нажимает") {
    const clap_event_midi_t half = makeMidi(0xB0, 64, 63);

    fixture.process(100, { &half.header });
    fixture.process(fadeFrames, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Педаль другого канала ноту не держит") {
    const clap_event_midi_t otherChannel = makeMidi(0xB1, 64, 127);

    fixture.process(100, { &otherChannel.header });
    fixture.process(fadeFrames, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("All notes off отпускает и ноты, и педаль") {
    const clap_event_midi_t allNotesOff = makeMidi(0xB0, 123, 0);

    fixture.process(100, { &pedalDown.header });
    fixture.process(fadeFrames, { &allNotesOff.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("All sound off заглушает ноту под педалью") {
    const clap_event_midi_t allSoundOff = makeMidi(0xB0, 120, 0);

    fixture.process(100, { &pedalDown.header });
    fixture.process(fadeFrames, { &allSoundOff.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("После all sound off педаль снята: следующая нота не зависает") {
    const clap_event_midi_t allSoundOff = makeMidi(0xB0, 120, 0);

    fixture.process(100, { &pedalDown.header });
    fixture.process(fadeFrames, { &allSoundOff.header });
    fixture.process(100, { &on.header });
    fixture.process(fadeFrames, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Команды паники другого канала ноту не трогают") {
    const clap_event_midi_t allNotesOff = makeMidi(0xB1, 123, 0);
    const clap_event_midi_t allSoundOff = makeMidi(0xB1, 120, 0);

    fixture.process(fadeFrames, { &allNotesOff.header, &allSoundOff.header });

    CHECK(peak(fixture.process(1000), 0, 1000) > 0.3f);
  }

  SUBCASE("Сброс хоста отпускает педаль: следующая нота не зависает") {
    fixture.process(100, { &pedalDown.header });
    fixture.plugin->reset(fixture.plugin);

    fixture.process(100, { &on.header });
    fixture.process(fadeFrames, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Другие контроллеры ноты не трогают") {
    const clap_event_midi_t modWheel = makeMidi(0xB0, 1, 127);

    fixture.process(fadeFrames, { &modWheel.header });

    CHECK(peak(fixture.process(1000), 0, 1000) > 0.3f);
  }

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

TEST_CASE("Pitch bend приходит через MIDI и сдвигает высоту на диапазон генератора (D-027)") {
  PluginFixture fixture;

  REQUIRE(fixture.load(buildPreset()));
  fixture.start();

  uint8_t lsb = 0;
  uint8_t msb = 0;
  int expected = 0;

  SUBCASE("Центр") {
    lsb = 0x00;
    msb = 0x40;
    expected = 440;
  }

  SUBCASE("Минимум: два полутона вниз") {
    lsb = 0x00;
    msb = 0x00;
    expected = 392;
  }

  SUBCASE("Максимум: почти два полутона вверх") {
    lsb = 0x7F;
    msb = 0x7F;
    expected = 494;
  }

  const clap_event_midi_t bend = makeMidi(0xE0, lsb, msb);
  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);

  const std::vector<float> audio = fixture.process(SAMPLE_RATE, { &bend.header, &on.header });

  CHECK(std::abs(countRisingCrossings(audio) - expected) <= 1);

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

TEST_CASE("События нот CLAP") {
  PluginFixture fixture;

  REQUIRE(fixture.load(buildPreset()));
  fixture.start();

  SUBCASE("Note on с нулевой velocity отпускает клавишу, а не запускает голос (D-047)") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
    const clap_event_note_t off = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4, 0.0);

    fixture.process(100, { &on.header });
    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Отпускание находит голос, даже если хост выдал событию новый note_id (D-047)") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4, 1.0, 184);
    const clap_event_note_t off = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4, 0.0, 185);

    fixture.process(100, { &on.header });
    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &off.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Note on с нулевой velocity без нажатой клавиши не создаёт голос (D-047)") {
    const clap_event_note_t off = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4, 0.0);

    CHECK(isSilent(fixture.process(1000, { &off.header }), 0, 1000));
  }

  SUBCASE("Нулевая velocity не отпускает другую клавишу и другой канал (D-047)") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
    const clap_event_note_t otherKey = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4 + 1, 0.0);
    clap_event_note_t otherChannel = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4, 0.0);

    otherChannel.channel = 1;

    fixture.process(100, { &on.header });
    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &otherKey.header, &otherChannel.header });

    CHECK(peak(fixture.process(1000), 0, 1000) > 0.3f);
  }

  SUBCASE("Note on без клавиши игнорируется") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, -1);

    CHECK(isSilent(fixture.process(1000, { &on.header }), 0, 1000));
  }

  SUBCASE("Нулевая velocity без клавиши ничего не отпускает") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
    const clap_event_note_t off = makeNote(CLAP_EVENT_NOTE_ON, -1, 0.0);

    fixture.process(100, { &on.header });
    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &off.header });

    CHECK(peak(fixture.process(1000), 0, 1000) > 0.3f);
  }

  SUBCASE("Choke быстро глушит ноту") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
    const clap_event_note_t choke = makeNote(CLAP_EVENT_NOTE_CHOKE, KEY_A4);

    fixture.process(100, { &on.header });
    fixture.process(FADE_FRAMES + CHUNK_FRAMES, { &choke.header });

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Reset сразу освобождает все голоса") {
    const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);

    fixture.process(100, { &on.header });
    fixture.plugin->reset(fixture.plugin);

    CHECK(isSilent(fixture.process(1000), 0, 1000));
  }

  SUBCASE("Событие в середине блока начинает ноту в свой сэмпл") {
    clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);
    on.header.time = 500;

    const std::vector<float> audio = fixture.process(1000, { &on.header });

    // Синус начинается с нуля, первый ненулевой сэмпл — следующий после события
    CHECK(isSilent(audio, 0, 501));
    CHECK(audio[501] != 0.0f);
  }

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

TEST_CASE("Плагин отвергает неверный пресет и остаётся в прежнем состоянии") {
  PluginFixture fixture;
  const std::string initial = fixture.save();

  CHECK_FALSE(fixture.load("not a preset"));
  CHECK_FALSE(fixture.load(""));
  CHECK_FALSE(fixture.load("{\"version\": 999}"));
  CHECK(fixture.save() == initial);

  CHECK(renderOffline({}, { .frameCount = 100, .preset = "not a preset" }).left.empty());
}

TEST_CASE("Состояние сохраняется и восстанавливается через потоки CLAP") {
  const std::string preset = buildPreset();

  PluginFixture source;

  // Потоки могут работать мелкими порциями
  REQUIRE(source.load(preset, 3));
  CHECK(withoutSession(source.save()) == preset);

  ChunkedWriter writer = { .chunkSize = 5 };
  REQUIRE(source.state->save(source.plugin, &writer.stream));
  CHECK(withoutSession(writer.text) == preset);

  PluginFixture restored;
  REQUIRE(restored.load(writer.text));
  CHECK(restored.save() == writer.text);
}

TEST_CASE("Ошибка потока отклоняет сохранение и загрузку") {
  PluginFixture fixture;

  ChunkedReader reader = { .text = buildPreset(), .chunkSize = 4096, .shouldFail = true };
  CHECK_FALSE(fixture.state->load(fixture.plugin, &reader.stream));

  ChunkedWriter writer = { .chunkSize = 4096, .shouldFail = true };
  CHECK_FALSE(fixture.state->save(fixture.plugin, &writer.stream));
}

TEST_CASE("Работающий плагин принимает новый граф, и он сразу виден в состоянии (D-054)") {
  PluginFixture fixture;
  const std::string preset = buildPreset(1000.0f);

  REQUIRE(fixture.load(preset));
  REQUIRE(fixture.plugin->activate(fixture.plugin, SAMPLE_RATE, 1, 256));

  CHECK(fixture.load(preset));
  CHECK(withoutSession(fixture.save()) == preset);

  CHECK(fixture.load(buildPreset(2000.0f)));
  CHECK(withoutSession(fixture.save()) == buildPreset(2000.0f));

  CHECK_FALSE(fixture.load("not a preset"));
  CHECK(withoutSession(fixture.save()) == buildPreset(2000.0f));

  fixture.plugin->deactivate(fixture.plugin);
}

namespace {
  uint32_t s_callbackRequests = 0;

  uint32_t emptyEventCount(const clap_input_events_t*) {
    return 0;
  }

  const clap_event_header_t* noEvent(const clap_input_events_t*, uint32_t) {
    return nullptr;
  }

  /**
   * Один блок звука, с нотами или без. Возвращает пик блока
   */
  float processBlock(const clap_plugin_t* plugin, const std::vector<clap_event_note_t>& notes = {}) {
    constexpr uint32_t FRAMES = 256;

    std::vector<float> left(FRAMES);
    std::vector<float> right(FRAMES);
    float* channels[2] = { left.data(), right.data() };
    clap_audio_buffer_t output{};
    output.data32 = channels;
    output.channel_count = 2;

    const clap_input_events_t events = {
      .ctx = (void*)&notes,
      .size = notes.empty() ? emptyEventCount : [](const clap_input_events_t* list) {
        return (uint32_t)((const std::vector<clap_event_note_t>*)list->ctx)->size();
      },
      .get = notes.empty() ? noEvent : [](const clap_input_events_t* list, uint32_t index) {
        return &(*(const std::vector<clap_event_note_t>*)list->ctx)[index].header;
      },
    };

    clap_process_t process{};
    process.frames_count = FRAMES;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &events;

    REQUIRE(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE);

    float peak = 0.0f;

    for (const float sample : left) {
      peak = std::max(peak, std::fabs(sample));
    }

    return peak;
  }
}

TEST_CASE("Проект открывается в порядке activate, state.load, start_processing: нота сразу идёт в его граф") {
  PluginFixture fixture;

  fixture.host.request_callback = [](const clap_host_t*) {
    s_callbackRequests++;
  };

  REQUIRE(fixture.plugin->activate(fixture.plugin, SAMPLE_RATE, 1, 256));
  REQUIRE(fixture.load(buildPreset()));
  REQUIRE(fixture.plugin->start_processing(fixture.plugin));

  const uint32_t requestsBefore = s_callbackRequests;

  // В пресете по умолчанию есть ENV с атакой, поэтому первый блок отличает графы по пику
  CHECK(processBlock(fixture.plugin, { makeNote(CLAP_EVENT_NOTE_ON, KEY_A4) }) == doctest::Approx(0.5f).epsilon(0.05));

  // Старый план надо освободить на главном потоке
  CHECK(s_callbackRequests > requestsBefore);

  fixture.plugin->on_main_thread(fixture.plugin);
  CHECK(processBlock(fixture.plugin) > 0.0f);

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

TEST_CASE("Смена состояния во время звука: on_main_thread доводит её до конца") {
  PluginFixture fixture;

  fixture.host.request_callback = [](const clap_host_t*) {
    s_callbackRequests++;
  };

  REQUIRE(fixture.load(buildPreset()));
  REQUIRE(fixture.plugin->activate(fixture.plugin, SAMPLE_RATE, 1, 256));
  REQUIRE(fixture.plugin->start_processing(fixture.plugin));

  processBlock(fixture.plugin, { makeNote(CLAP_EVENT_NOTE_ON, KEY_A4) });

  REQUIRE(fixture.load(buildPreset(2000.0f)));

  for (int block = 0; block < 8; block++) {
    processBlock(fixture.plugin);
    fixture.plugin->on_main_thread(fixture.plugin);
  }

  // Старая нота закончилась вместе со старым графом
  CHECK(processBlock(fixture.plugin) == 0.0f);

  fixture.plugin->stop_processing(fixture.plugin);
  fixture.plugin->deactivate(fixture.plugin);
}

namespace {
  /**
   * Нота, которую LFO главного графа открывает и закрывает меандром с периодом в четверть
   */
  std::string buildSyncedGatePreset() {
    ModuleRegistry registry;

    REQUIRE(addAllModules(registry).has_value());

    SynthModel model(&registry);

    const auto add = [&model](const char* type, std::optional<ModuleId> owner = std::nullopt) {
      return **model.apply(AddModule{ type, owner });
    };

    const ModuleId generator = add("generator");
    const ModuleId osc = add("oscillator", generator);
    const ModuleId amp = add("amp");
    const ModuleId lfo = add("lfo");

    ModuleId voiceOutput = 0;

    for (const auto& [id, module] : model.getModules()) {
      if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
        voiceOutput = id;
      }
    }

    for (const Command& command : std::initializer_list<Command>{
      Connect{ { osc, "out", voiceOutput, "in" } },
      Connect{ { generator, "out", amp, "in" } },
      Connect{ { amp, "out", model.getOutputId(), "in" } },
      SetParameter{ amp, "gain", 0.0f },
      SetParameter{ lfo, "wave", 3.0f },
      SetParameter{ lfo, "sync", 1.0f },
      AddRoute{ { { lfo, "out" }, { amp, "gain" }, 1.0f } },
    }) {
      REQUIRE(model.apply(command).has_value());
    }

    return saveModel(model);
  }

  /**
   * Сколько раз звук появился после тишины. Конец рендера не считается:
   * туда попадает начало следующего периода
   */
  int countBursts(const std::vector<float>& channel) {
    constexpr uint32_t WINDOW = 480;

    int bursts = 0;
    bool wasLoud = false;

    for (uint32_t from = 0; from + 3 * WINDOW <= channel.size(); from += WINDOW) {
      const bool isLoud = peak(channel, from, from + WINDOW) > 0.001f;

      bursts += isLoud && !wasLoud ? 1 : 0;
      wasLoud = isLoud;
    }

    return bursts;
  }
}

TEST_CASE("Темп из транспорта задаёт скорость LFO с синхронизацией (D-050)") {
  const uint32_t frameCount = 2 * SAMPLE_RATE;
  const std::string preset = buildSyncedGatePreset();
  const std::vector<OfflineNote> notes = { { .key = KEY_A4, .startFrame = 0, .endFrame = frameCount } };

  double tempo = 0.0;
  int bursts = 0;

  // За 2 секунды при 120 BPM проходит 4 четверти
  SUBCASE("Хост без транспорта: 120 BPM") {
    tempo = 0.0;
    bursts = 4;
  }

  SUBCASE("60 BPM") {
    tempo = 60.0;
    bursts = 2;
  }

  SUBCASE("240 BPM") {
    tempo = 240.0;
    bursts = 8;
  }

  const StereoBuffer audio = renderOffline(notes, { .frameCount = frameCount, .preset = preset, .tempo = tempo });

  REQUIRE(audio.left.size() == frameCount);
  CHECK(countBursts(audio.left) == bursts);
}

TEST_CASE("Транспорт без темпа даёт темп по умолчанию, событие транспорта меняет темп (D-050)") {
  PluginFixture fixture;

  REQUIRE(fixture.load(buildSyncedGatePreset()));
  fixture.start();

  clap_event_transport_t transport{};
  transport.header.size = sizeof(clap_event_transport_t);
  transport.header.time = 0;
  transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  transport.header.type = CLAP_EVENT_TRANSPORT;
  transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
  transport.tempo = 240.0;

  const clap_event_note_t on = makeNote(CLAP_EVENT_NOTE_ON, KEY_A4);

  SUBCASE("С темпом") {
    CHECK(countBursts(fixture.process(2 * SAMPLE_RATE, { &transport.header, &on.header })) == 8);
  }

  SUBCASE("Флага темпа нет") {
    transport.flags = 0;

    CHECK(countBursts(fixture.process(2 * SAMPLE_RATE, { &transport.header, &on.header })) == 4);
  }
}

TEST_CASE("Примеры пресетов из examples/ загружаются и звучат") {
  std::string path = SINT_EXAMPLES_DIR;

  SUBCASE("Две пилы") {
    path += "/two-saws.sintpreset";
  }

  SUBCASE("Демонстрация модуляции") {
    path += "/modulation-demo.sintpreset";
  }

  std::ifstream file(path, std::ios::binary);
  std::ostringstream content;

  REQUIRE(file.good());
  content << file.rdbuf();

  const StereoBuffer audio = renderOffline(heldNote(45), { .frameCount = 4800, .preset = content.str() });

  REQUIRE(audio.left.size() == 4800);
  CHECK(peak(audio.left, 0, 4800) > 0.01f);
}
