#include <doctest/doctest.h>

#include <cstdint>
#include <set>

#include "../engine/voicePool/voicePool.h"

namespace {
  NoteAddress note(int16_t key, int32_t noteId = -1) {
    return { noteId, 0, 0, key };
  }

  /**
   * Сколько голосов находится на заданной стадии
   */
  int countStage(const VoicePool& pool, VOICE_STAGE stage) {
    int count = 0;

    for (const VoiceState& voice : pool.getVoices()) {
      count += voice.stage == stage ? 1 : 0;
    }

    return count;
  }

  VoicePool createPool(uint32_t limit) {
    VoicePool pool;

    pool.setLimit(limit);

    return pool;
  }
}

TEST_CASE("Новая нота занимает голос и запоминает адрес и velocity") {
  VoicePool pool = createPool(4);

  const uint32_t slot = pool.noteOn({ 7, 1, 2, 60 }, 0.5f);
  const VoiceState& voice = pool.getVoices()[slot];

  CHECK(voice.stage == VOICE_STAGE::HELD);
  CHECK(voice.note.noteId == 7);
  CHECK(voice.note.port == 1);
  CHECK(voice.note.channel == 2);
  CHECK(voice.note.key == 60);
  CHECK(voice.velocity == 0.5f);
  CHECK(voice.fade == 1.0f);
}

TEST_CASE("Ноты в пределах лимита занимают разные голоса и никого не забирают") {
  VoicePool pool = createPool(4);
  std::set<uint32_t> slots;

  for (int16_t key = 60; key < 64; key++) {
    slots.insert(pool.noteOn(note(key), 1.0f));
  }

  CHECK(slots.size() == 4);
  CHECK(countStage(pool, VOICE_STAGE::HELD) == 4);
  CHECK(countStage(pool, VOICE_STAGE::FADING) == 0);
}

TEST_CASE("Лимит не бывает меньше одного голоса") {
  VoicePool pool = createPool(0);

  pool.noteOn(note(60), 1.0f);
  CHECK(countStage(pool, VOICE_STAGE::HELD) == 1);
}

TEST_CASE("При занятом пуле забирается самый старый из удерживаемых (D-042)") {
  VoicePool pool = createPool(3);

  const uint32_t oldest = pool.noteOn(note(60), 1.0f);
  const uint32_t middle = pool.noteOn(note(61), 1.0f);
  const uint32_t newest = pool.noteOn(note(62), 1.0f);
  const uint32_t added = pool.noteOn(note(63), 1.0f);

  CHECK(pool.getVoices()[oldest].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[middle].stage == VOICE_STAGE::HELD);
  CHECK(pool.getVoices()[newest].stage == VOICE_STAGE::HELD);
  CHECK(pool.getVoices()[added].stage == VOICE_STAGE::HELD);
  CHECK(added != oldest);
}

TEST_CASE("Отпущенный голос забирается раньше удерживаемого, даже если тот старше (D-042)") {
  VoicePool pool = createPool(3);

  const uint32_t heldOldest = pool.noteOn(note(60), 1.0f);
  const uint32_t released = pool.noteOn(note(61), 1.0f);
  const uint32_t held = pool.noteOn(note(62), 1.0f);

  pool.noteOff(note(61));
  pool.noteOn(note(63), 1.0f);

  CHECK(pool.getVoices()[released].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[heldOldest].stage == VOICE_STAGE::HELD);
  CHECK(pool.getVoices()[held].stage == VOICE_STAGE::HELD);
}

TEST_CASE("Среди отпущенных забирается самый старый по времени ноты (D-042)") {
  VoicePool pool = createPool(3);

  const uint32_t older = pool.noteOn(note(60), 1.0f);
  const uint32_t newer = pool.noteOn(note(61), 1.0f);

  pool.noteOn(note(62), 1.0f);

  // Порядок отпускания обратный: важен возраст ноты
  pool.noteOff(note(61));
  pool.noteOff(note(60));
  pool.noteOn(note(63), 1.0f);

  CHECK(pool.getVoices()[older].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[newer].stage == VOICE_STAGE::RELEASED);
}

TEST_CASE("Затухающие голоса не считаются занятыми") {
  VoicePool pool = createPool(2);

  const uint32_t first = pool.noteOn(note(60), 1.0f);
  const uint32_t second = pool.noteOn(note(61), 1.0f);

  pool.startFade(first);
  pool.noteOn(note(62), 1.0f);

  CHECK(pool.getVoices()[second].stage == VOICE_STAGE::HELD);
  CHECK(countStage(pool, VOICE_STAGE::HELD) == 2);
}

TEST_CASE("Много нот подряд не выходят за лимит и всегда находят слот") {
  VoicePool pool = createPool(2);

  for (int16_t key = 0; key < 100; key++) {
    const uint32_t slot = pool.noteOn(note(key), 1.0f);

    REQUIRE(slot < pool.getVoices().size());
    CHECK(pool.getVoices()[slot].note.key == key);
    CHECK(countStage(pool, VOICE_STAGE::HELD) <= 2);
  }

  CHECK(countStage(pool, VOICE_STAGE::HELD) == 2);
}

TEST_CASE("Голос освобождается, когда затухание дошло до нуля") {
  VoicePool pool = createPool(2);
  const uint32_t slot = pool.noteOn(note(60), 1.0f);

  pool.startFade(slot);
  pool.setFade(slot, 0.5f);

  CHECK(pool.getVoices()[slot].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[slot].fade == 0.5f);

  pool.setFade(slot, 0.0f);

  CHECK(pool.getVoices()[slot].stage == VOICE_STAGE::IDLE);
}

TEST_CASE("Освобождённый голос снова доступен и начинает с полной громкости") {
  VoicePool pool = createPool(1);
  const uint32_t slot = pool.noteOn(note(60), 1.0f);

  pool.startFade(slot);
  pool.setFade(slot, 0.0f);

  const uint32_t reused = pool.noteOn(note(61), 0.25f);

  CHECK(pool.getVoices()[reused].stage == VOICE_STAGE::HELD);
  CHECK(pool.getVoices()[reused].fade == 1.0f);
  CHECK(pool.getVoices()[reused].velocity == 0.25f);
}

TEST_CASE("Note off ищет голос по всему адресу, -1 означает любое значение") {
  VoicePool pool = createPool(4);

  const uint32_t first = pool.noteOn({ 1, 0, 0, 60 }, 1.0f);
  const uint32_t second = pool.noteOn({ 2, 0, 0, 60 }, 1.0f);
  const uint32_t otherChannel = pool.noteOn({ 3, 0, 1, 60 }, 1.0f);
  const uint32_t otherKey = pool.noteOn({ 4, 0, 0, 61 }, 1.0f);

  const auto stageOf = [&pool](uint32_t slot) {
    return pool.getVoices()[slot].stage;
  };

  SUBCASE("По note_id отпускается только один голос той же клавиши") {
    pool.noteOff({ 2, 0, 0, 60 });

    CHECK(stageOf(first) == VOICE_STAGE::HELD);
    CHECK(stageOf(second) == VOICE_STAGE::RELEASED);
  }

  SUBCASE("Хост может выдать note_id только при нажатии") {
    pool.noteOff({ -1, 0, 0, 60 });

    CHECK(stageOf(first) == VOICE_STAGE::RELEASED);
    CHECK(stageOf(second) == VOICE_STAGE::RELEASED);
    CHECK(stageOf(otherChannel) == VOICE_STAGE::HELD);
    CHECK(stageOf(otherKey) == VOICE_STAGE::HELD);
  }

  SUBCASE("Канал задан, клавиша любая") {
    pool.noteOff({ -1, 0, 1, -1 });

    CHECK(stageOf(first) == VOICE_STAGE::HELD);
    CHECK(stageOf(otherChannel) == VOICE_STAGE::RELEASED);
  }

  SUBCASE("Порт не совпал") {
    pool.noteOff({ -1, 1, 0, 60 });

    CHECK(stageOf(first) == VOICE_STAGE::HELD);
  }

  SUBCASE("Все значения любые") {
    pool.noteOff({ -1, -1, -1, -1 });

    CHECK(countStage(pool, VOICE_STAGE::RELEASED) == 4);
  }
}

TEST_CASE("Голос без note_id отпускается и событием с note_id") {
  VoicePool pool = createPool(2);
  const uint32_t slot = pool.noteOn({ -1, 0, 0, 60 }, 1.0f);

  pool.noteOff({ 5, 0, 0, 60 });

  CHECK(pool.getVoices()[slot].stage == VOICE_STAGE::RELEASED);
}

TEST_CASE("Note off не трогает отпущенные и затухающие голоса") {
  VoicePool pool = createPool(2);
  const uint32_t slot = pool.noteOn(note(60), 1.0f);

  pool.startFade(slot);
  pool.noteOff(note(60));

  CHECK(pool.getVoices()[slot].stage == VOICE_STAGE::FADING);
}

TEST_CASE("Choke запускает затухание и удерживаемых, и отпущенных голосов") {
  VoicePool pool = createPool(3);

  const uint32_t held = pool.noteOn(note(60), 1.0f);
  const uint32_t released = pool.noteOn(note(60), 1.0f);
  const uint32_t other = pool.noteOn(note(61), 1.0f);

  pool.noteOff({ -1, 0, 0, 60 });

  const uint32_t heldAgain = pool.noteOn(note(60), 1.0f);

  pool.choke(note(60));

  CHECK(pool.getVoices()[held].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[released].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[heldAgain].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[other].stage == VOICE_STAGE::HELD);
}

TEST_CASE("Choke не отпускает свободные голоса") {
  VoicePool pool = createPool(2);

  pool.choke({ -1, -1, -1, -1 });

  CHECK(countStage(pool, VOICE_STAGE::IDLE) == (int)pool.getVoices().size());
}

TEST_CASE("Clear сразу освобождает все голоса") {
  VoicePool pool = createPool(2);

  pool.noteOn(note(60), 1.0f);
  pool.noteOn(note(61), 1.0f);
  pool.noteOn(note(62), 1.0f);
  pool.clear();

  CHECK(countStage(pool, VOICE_STAGE::IDLE) == (int)pool.getVoices().size());
}

TEST_CASE("Уменьшение лимита забирает лишние голоса в порядке D-042") {
  VoicePool pool = createPool(4);
  const uint32_t oldest = pool.noteOn(note(60), 1.0f);
  const uint32_t released = pool.noteOn(note(61), 1.0f);

  pool.noteOn(note(62), 1.0f);
  pool.noteOn(note(63), 1.0f);
  pool.noteOff(note(61));

  pool.setLimit(2);

  CHECK(countStage(pool, VOICE_STAGE::FADING) == 2);
  CHECK(pool.getVoices()[released].stage == VOICE_STAGE::FADING);
  CHECK(pool.getVoices()[oldest].stage == VOICE_STAGE::FADING);
}

TEST_CASE("Уменьшение лимита не убирает слоты: на них могут звучать голоса") {
  VoicePool pool = createPool(4);
  const size_t slots = pool.getVoices().size();

  pool.setLimit(1);

  CHECK(pool.getVoices().size() == slots);
}

TEST_CASE("Лимит голосов ограничен пределом, под который заранее выделена память") {
  VoicePool pool = createPool(1);
  const VoiceState* voices = pool.getVoices().data();

  pool.setLimit(MAX_POLYPHONY * 2);

  CHECK(pool.getVoices().size() == MAX_VOICE_SLOTS);
  CHECK(pool.getVoices().data() == voices);
}
