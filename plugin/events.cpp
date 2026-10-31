#include "./plugin.h"

namespace {
  constexpr uint8_t MIDI_NOTE_OFF = 0x80;
  constexpr uint8_t MIDI_NOTE_ON = 0x90;
  constexpr uint8_t MIDI_CONTROL_CHANGE = 0xB0;
  constexpr uint8_t MIDI_PITCH_BEND = 0xE0;
  constexpr uint8_t CC_SUSTAIN = 64;
  constexpr uint8_t CC_SUSTAIN_DOWN = 64; // Значение, с которого педаль считается нажатой
  constexpr uint8_t CC_ALL_SOUND_OFF = 120;
  constexpr uint8_t CC_ALL_NOTES_OFF = 123;
  constexpr float MIDI_MAX_VALUE = 127.0f;
  constexpr float MIDI_PITCH_BEND_CENTER = 8192.0f;

  NoteAddress toAddress(const clap_event_note_t *ev) {
    return { ev->note_id, ev->port_index, ev->channel, ev->key };
  }

  /**
   * Педаль приходит сырым MIDI, даже когда хост шлёт ноты событиями CLAP.
   * Reaper при остановке и перемотке сам присылает CC64 = 0 (D-058)
   */
  void processControlChange(MyPluginInstance *instance, int16_t channel, uint8_t controller, uint8_t value) {
    if (controller == CC_SUSTAIN) {
      instance->engine.setSustain(channel, value >= CC_SUSTAIN_DOWN);
    } else if (controller == CC_ALL_NOTES_OFF) {
      instance->engine.allNotesOff(channel);
    } else if (controller == CC_ALL_SOUND_OFF) {
      instance->engine.allSoundOff(channel);
    }
  }

  /**
   * Сырое MIDI: так приходит pitch bend, а хост может прислать в таком виде и ноты
   */
  void processMidi(MyPluginInstance *instance, const clap_event_midi_t *ev) {
    const uint8_t status = ev->data[0] & 0xF0;
    const NoteAddress note = { -1, (int16_t)ev->port_index, (int16_t)(ev->data[0] & 0x0F), ev->data[1] };

    if (status == MIDI_NOTE_ON && ev->data[2] > 0) {
      instance->engine.noteOn(note, (float)ev->data[2] / MIDI_MAX_VALUE);
    } else if (status == MIDI_NOTE_OFF || status == MIDI_NOTE_ON) {
      // В MIDI 1 note on с нулевой velocity означает отпускание, в отличие от события CLAP
      instance->engine.noteOff(note);
    } else if (status == MIDI_CONTROL_CHANGE) {
      processControlChange(instance, note.channel, ev->data[1], ev->data[2]);
    } else if (status == MIDI_PITCH_BEND) {
      const int value = ev->data[1] | (ev->data[2] << 7);

      instance->engine.setPitchBend(((float)value - MIDI_PITCH_BEND_CENTER) / MIDI_PITCH_BEND_CENTER);
    }
  }
}

/**
 * Темп для LFO с синхронизацией. Хост может не иметь транспорта или не сообщать темп (D-050)
 */
void my_plug_apply_transport(MyPluginInstance *instance, const clap_event_transport_t *transport) {
  const bool hasTempo = transport && (transport->flags & CLAP_TRANSPORT_HAS_TEMPO);

  instance->engine.setTempo(hasTempo ? (float)transport->tempo : DEFAULT_TEMPO);
}

/**
 * Функция для обработки событий
 */
void my_plug_process_event(MyPluginInstance *instance, const clap_event_header_t *event_header) {
    if (event_header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
      switch (event_header->type) {
        case CLAP_EVENT_NOTE_ON: {
          const clap_event_note_t *ev = (const clap_event_note_t *)event_header;

          // Note on без порта, канала или клавиши CLAP разрешает игнорировать
          if (ev->port_index < 0 || ev->channel < 0 || ev->key < 0) {
            break;
          }

          // Отступление от CLAP (D-047): старые клавиатуры отпускают клавишу через note on
          // с нулевой velocity, а хост может передать это как обычную ноту. У такого события
          // может быть свой note_id, поэтому голос ищется только по порту, каналу и клавише
          if (ev->velocity == 0.0) {
            instance->engine.noteOff({ -1, ev->port_index, ev->channel, ev->key });
          } else {
            instance->engine.noteOn(toAddress(ev), (float)ev->velocity);
          }
          break;
        }

        case CLAP_EVENT_NOTE_OFF: {
          const clap_event_note_t *ev = (const clap_event_note_t *)event_header;

          instance->engine.noteOff(toAddress(ev));
          break;
        }

        case CLAP_EVENT_NOTE_CHOKE: {
          const clap_event_note_t *ev = (const clap_event_note_t *)event_header;

          instance->engine.choke(toAddress(ev));
          break;
        }

        case CLAP_EVENT_NOTE_EXPRESSION: {
          // const clap_event_note_expression_t *ev = (const clap_event_note_expression_t *)event_header;
          // TODO: handle note expression
          break;
        }

        case CLAP_EVENT_PARAM_VALUE: {
          my_plug_set_slot(instance, (const clap_event_param_value_t *)event_header);
          break;
        }

        case CLAP_EVENT_PARAM_MOD: {
          // const clap_event_param_mod_t *ev = (const clap_event_param_mod_t *)event_header;
          // TODO: handle parameter modulation
          break;
        }

        case CLAP_EVENT_TRANSPORT: {
          my_plug_apply_transport(instance, (const clap_event_transport_t *)event_header);
          break;
        }

        case CLAP_EVENT_MIDI: {
          processMidi(instance, (const clap_event_midi_t *)event_header);
          break;
        }

        case CLAP_EVENT_MIDI_SYSEX: {
          // const clap_event_midi_sysex_t *ev = (const clap_event_midi_sysex_t *)event_header;
          // TODO: handle MIDI Sysex event
          break;
        }

        case CLAP_EVENT_MIDI2: {
          // const clap_event_midi2_t *ev = (const clap_event_midi2_t *)event_header;
          // TODO: handle MIDI2 event
          break;
        }
      }
    }
}