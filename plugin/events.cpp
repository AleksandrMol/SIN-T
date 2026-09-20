#include "./plugin.h"

/**
 * Функция для обработки событий
 */
void my_plug_process_event(MyPluginInstance *instance, const clap_event_header_t *event_header) {
    if (event_header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
      switch (event_header->type) {
        case CLAP_EVENT_NOTE_ON: {
          const clap_event_note_t *ev = (const clap_event_note_t *)event_header;
            if (ev->velocity == 0.0) {
              instance->gen.noteOff(ev->key);
            } else {
              instance->gen.noteOn(ev->key);
            }
          break;
        }

        case CLAP_EVENT_NOTE_OFF: {
          const clap_event_note_t *ev = (const clap_event_note_t *)event_header;

          instance->gen.noteOff(ev->key);
          break;
        }

        case CLAP_EVENT_NOTE_CHOKE: {
          const clap_event_note_t *ev = (const clap_event_note_t *)event_header;
          // TODO: handle note choke
          break;
        }

        case CLAP_EVENT_NOTE_EXPRESSION: {
          const clap_event_note_expression_t *ev = (const clap_event_note_expression_t *)event_header;
          // TODO: handle note expression
          break;
        }

        case CLAP_EVENT_PARAM_VALUE: {
          const clap_event_param_value_t *ev = (const clap_event_param_value_t *)event_header;
          // TODO: handle parameter change
          break;
        }

        case CLAP_EVENT_PARAM_MOD: {
          const clap_event_param_mod_t *ev = (const clap_event_param_mod_t *)event_header;
          // TODO: handle parameter modulation
          break;
        }

        case CLAP_EVENT_TRANSPORT: {
          const clap_event_transport_t *ev = (const clap_event_transport_t *)event_header;
          // TODO: handle transport event
          break;
        }

        case CLAP_EVENT_MIDI: {
          const clap_event_midi_t *ev = (const clap_event_midi_t *)event_header;
          // TODO: handle MIDI event
          break;
        }

        case CLAP_EVENT_MIDI_SYSEX: {
          const clap_event_midi_sysex_t *ev = (const clap_event_midi_sysex_t *)event_header;
          // TODO: handle MIDI Sysex event
          break;
        }

        case CLAP_EVENT_MIDI2: {
          const clap_event_midi2_t *ev = (const clap_event_midi2_t *)event_header;
          // TODO: handle MIDI2 event
          break;
        }
      }
    }
}