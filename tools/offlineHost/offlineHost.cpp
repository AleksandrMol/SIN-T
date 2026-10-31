#include "./offlineHost.h"

#include <algorithm>
#include <cstring>

#include "../../clap/clap.h"

namespace {
  const clap_host_t s_host = {
    .clap_version = CLAP_VERSION_INIT,
    .host_data = nullptr,
    .name = "SIN-T offline host",
    .vendor = "ChillySwim",
    .url = "",
    .version = "1.0.0",
    .get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    .request_restart = [](const clap_host_t*) {},
    .request_process = [](const clap_host_t*) {},
    .request_callback = [](const clap_host_t*) {},
  };

  /**
   * Все ноты рендера как события CLAP, по возрастанию времени
   */
  std::vector<clap_event_note_t> buildEvents(const std::vector<OfflineNote>& notes) {
    std::vector<clap_event_note_t> events;
    events.reserve(notes.size() * 2);

    int32_t noteId = 0;

    const auto makeEvent = [&](const OfflineNote& note, uint16_t type, uint32_t time, double velocity) {
      clap_event_note_t event{};
      event.header.size = sizeof(clap_event_note_t);
      event.header.time = time;
      event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      event.header.type = type;
      event.note_id = noteId;
      event.port_index = 0;
      event.channel = 0;
      event.key = note.key;
      event.velocity = velocity;
      return event;
    };

    for (const OfflineNote& note : notes) {
      events.push_back(makeEvent(note, CLAP_EVENT_NOTE_ON, note.startFrame, note.velocity));
      events.push_back(makeEvent(note, CLAP_EVENT_NOTE_OFF, note.endFrame, 0.0));
      noteId++;
    }

    std::stable_sort(events.begin(), events.end(), [](const clap_event_note_t& a, const clap_event_note_t& b) {
      return a.header.time < b.header.time;
    });

    return events;
  }

  /**
   * События одного блока. Время в CLAP отсчитывается от начала блока
   */
  struct BlockEvents {
    std::vector<clap_event_note_t> events;

    static uint32_t size(const clap_input_events_t* list) {
      return (uint32_t)((BlockEvents*)list->ctx)->events.size();
    }

    static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t index) {
      return &((BlockEvents*)list->ctx)->events[index].header;
    }
  };

  /**
   * Передаёт плагину пресет так же, как DAW передаёт сохранённое состояние проекта
   */
  bool loadPreset(const clap_plugin_t* plugin, const std::string& preset) {
    struct ReadPosition {
      const std::string& text;
      size_t offset;
    };

    const auto* state = (const clap_plugin_state_t*)plugin->get_extension(plugin, CLAP_EXT_STATE);

    if (!state) {
      return false;
    }

    ReadPosition position = { preset, 0 };

    const clap_istream_t stream = {
      .ctx = &position,
      .read = [](const clap_istream_t* source, void* buffer, uint64_t size) -> int64_t {
        auto* reader = (ReadPosition*)source->ctx;
        const size_t count = std::min((size_t)size, reader->text.size() - reader->offset);

        std::memcpy(buffer, reader->text.data() + reader->offset, count);
        reader->offset += count;

        return (int64_t)count;
      },
    };

    return state->load(plugin, &stream);
  }

  bool processAll(
    const clap_plugin_t* plugin,
    const std::vector<OfflineNote>& notes,
    const OfflineRenderSettings& settings,
    StereoBuffer& audio
  ) {
    const std::vector<clap_event_note_t> events = buildEvents(notes);
    size_t nextEvent = 0;

    BlockEvents blockEvents;
    const clap_input_events_t inEvents = {
      .ctx = &blockEvents,
      .size = BlockEvents::size,
      .get = BlockEvents::get,
    };
    const clap_output_events_t outEvents = {
      .ctx = nullptr,
      .try_push = [](const clap_output_events_t*, const clap_event_header_t*) { return true; },
    };

    clap_event_transport_t transport{};
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
    transport.tempo = settings.tempo;

    audio.left.assign(settings.frameCount, 0.0f);
    audio.right.assign(settings.frameCount, 0.0f);

    for (uint32_t offset = 0; offset < settings.frameCount; offset += settings.blockSize) {
      const uint32_t frames = std::min(settings.blockSize, settings.frameCount - offset);

      blockEvents.events.clear();
      while (nextEvent < events.size() && events[nextEvent].header.time < offset + frames) {
        clap_event_note_t event = events[nextEvent];
        event.header.time -= offset;
        blockEvents.events.push_back(event);
        nextEvent++;
      }

      float* channels[2] = { audio.left.data() + offset, audio.right.data() + offset };
      clap_audio_buffer_t output{};
      output.data32 = channels;
      output.channel_count = 2;

      clap_process_t process{};
      process.steady_time = offset;
      process.frames_count = frames;
      process.audio_outputs = &output;
      process.audio_outputs_count = 1;
      process.in_events = &inEvents;
      process.out_events = &outEvents;
      process.transport = settings.tempo > 0.0 ? &transport : nullptr;

      if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR) {
        return false;
      }
    }

    return true;
  }
}

StereoBuffer renderOffline(const std::vector<OfflineNote>& notes, const OfflineRenderSettings& settings) {
  StereoBuffer audio;

  if (settings.blockSize == 0 || !clap_entry.init("")) {
    return audio;
  }

  const auto* factory = (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  const clap_plugin_descriptor_t* descriptor = factory ? factory->get_plugin_descriptor(factory, 0) : nullptr;
  const clap_plugin_t* plugin = descriptor ? factory->create_plugin(factory, &s_host, descriptor->id) : nullptr;

  if (plugin) {
    bool rendered = false;

    if (plugin->init(plugin) && (settings.preset.empty() || loadPreset(plugin, settings.preset))) {
      if (plugin->activate(plugin, settings.sampleRate, 1, settings.blockSize)) {
        if (plugin->start_processing(plugin)) {
          rendered = processAll(plugin, notes, settings, audio);
          plugin->stop_processing(plugin);
        }
        plugin->deactivate(plugin);
      }
    }

    plugin->destroy(plugin);

    if (!rendered) {
      audio = {};
    }
  }

  clap_entry.deinit();

  return audio;
}
