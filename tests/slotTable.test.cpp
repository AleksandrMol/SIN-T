#include <algorithm>
#include <string>
#include <vector>

#include "../engine/preset/preset.h"
#include "../engine/sessionState/sessionState.h"
#include "../engine/slotTable/slotInbox.h"
#include "../engine/slotTable/slotOutbox.h"
#include "../engine/slotTable/slotTable.h"
#include "./modelFixture.h"

namespace {
  /**
   * Первый слот пула
   */
  constexpr uint32_t FIRST_POOL_SLOT = MACRO_COUNT;

  uint32_t countBound(const SlotTable& table) {
    uint32_t count = 0;

    for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
      count += table.find(slot) ? 1 : 0;
    }

    return count;
  }

  struct SlotFixture : Fixture {
    SlotTable table;

    void sync() {
      this->table.sync(*this->model);
    }

    /**
     * Номер слота параметра или SLOT_COUNT, если параметр не экспортирован
     */
    uint32_t findSlot(ModuleId module, const std::string& parameter) const {
      const auto& descriptors = this->model->findModule(module)->descriptor->parameters;
      const auto index = (uint32_t)(std::ranges::find_if(descriptors, [&parameter](const ParameterDescriptor& descriptor) {
        return descriptor.key == parameter;
      }) - descriptors.begin());

      for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
        const SlotInfo* info = this->table.find(slot);

        if (info && info->target.module == module && info->target.parameter == index) {
          return slot;
        }
      }

      return SLOT_COUNT;
    }
  };
}

TEST_CASE("Список хоста постоянный: 8 макросов и пул из 1024 слотов (D-011)") {
  // Числа менять нельзя: от них зависят проекты пользователей
  CHECK(MACRO_COUNT == 8);
  CHECK(POOL_SLOT_COUNT == 1024);
  CHECK(SLOT_COUNT == 1032);
}

TEST_CASE_FIXTURE(SlotFixture, "Макросы занимают первые слоты всегда, даже без модулей") {
  this->sync();

  CHECK(countBound(this->table) == MACRO_COUNT);

  for (uint32_t slot = 0; slot < MACRO_COUNT; slot++) {
    const SlotInfo* info = this->table.find(slot);

    REQUIRE(info != nullptr);
    CHECK(info->target.module == MACROS_MODULE_ID);
    CHECK(info->target.parameter == slot);
    CHECK(info->name == "Macro " + std::to_string(slot + 1));
    CHECK(info->path == "Macros");
  }
}

TEST_CASE_FIXTURE(SlotFixture, "Параметр занимает слот при создании модуля и освобождает при удалении (D-011)") {
  const ModuleId filter = this->add("test.filter");

  this->sync();

  CHECK(this->findSlot(filter, "cutoff") == FIRST_POOL_SLOT);
  CHECK(this->findSlot(filter, "drive") == FIRST_POOL_SLOT + 1);

  const SlotTable before = this->table;

  REQUIRE(this->model->apply(RemoveModule{ filter }).has_value());
  this->sync();

  CHECK(this->table.find(FIRST_POOL_SLOT) == nullptr);
  CHECK(this->table.find(FIRST_POOL_SLOT + 1) == nullptr);
  CHECK(this->table.findReplaced(before) == std::vector<uint32_t>{ FIRST_POOL_SLOT, FIRST_POOL_SLOT + 1 });
}

TEST_CASE_FIXTURE(SlotFixture, "Занятые слоты не меняются при добавлении и удалении других модулей") {
  const ModuleId first = this->add("test.filter");
  const ModuleId second = this->add("test.filter");

  this->sync();

  const uint32_t kept = this->findSlot(second, "cutoff");

  REQUIRE(this->model->apply(RemoveModule{ first }).has_value());
  this->sync();
  this->add("test.lfo");
  this->sync();

  CHECK(this->findSlot(second, "cutoff") == kept);
}

TEST_CASE_FIXTURE(SlotFixture, "Новый параметр занимает первый свободный слот (D-011)") {
  const ModuleId first = this->add("test.filter");

  this->add("test.filter");
  this->sync();

  REQUIRE(this->model->apply(RemoveModule{ first }).has_value());
  this->sync();

  const ModuleId lfo = this->add("test.lfo");

  this->sync();

  CHECK(this->findSlot(lfo, "rate") == FIRST_POOL_SLOT);
}

TEST_CASE_FIXTURE(SlotFixture, "Параметры без слота работают, но не экспортируются, и получают освободившийся слот (D-011)") {
  std::vector<ModuleId> filters;

  // По два параметра у каждого: два последних параметра слота не получат
  for (uint32_t index = 0; index < POOL_SLOT_COUNT / 2 + 1; index++) {
    filters.push_back(this->add("test.filter"));
  }

  this->sync();

  const ModuleId last = filters.back();

  CHECK(countBound(this->table) == SLOT_COUNT);
  CHECK(this->findSlot(last, "cutoff") == SLOT_COUNT);

  REQUIRE(this->model->apply(RemoveModule{ filters.front() }).has_value());
  this->sync();

  CHECK(this->findSlot(last, "cutoff") == FIRST_POOL_SLOT);
  CHECK(this->findSlot(last, "drive") == FIRST_POOL_SLOT + 1);
}

TEST_CASE_FIXTURE(SlotFixture, "Имена показывают модуль и генератор: Reaper видит только имя (D-057)") {
  const ModuleId generator = this->add("generator");
  const ModuleId first = this->add("test.filter", generator);
  const ModuleId second = this->add("test.filter", generator);
  const ModuleId lfo = this->add("test.lfo");

  const ModuleId otherGenerator = this->add("generator");
  const ModuleId otherFilter = this->add("test.filter", otherGenerator);

  this->sync();

  const auto info = [this](ModuleId module, const std::string& parameter) {
    const SlotInfo* found = this->table.find(this->findSlot(module, parameter));

    REQUIRE(found != nullptr);

    return std::pair(found->name, found->path);
  };

  CHECK(info(generator, "polyphony") == std::pair<std::string, std::string>("G1 Polyphony", "Generator 1"));
  CHECK(info(first, "cutoff") == std::pair<std::string, std::string>("G1 Test filter 1 Cutoff", "Generator 1/Test filter 1"));
  CHECK(info(second, "cutoff") == std::pair<std::string, std::string>("G1 Test filter 2 Cutoff", "Generator 1/Test filter 2"));
  CHECK(info(otherFilter, "cutoff") == std::pair<std::string, std::string>("G2 Test filter 1 Cutoff", "Generator 2/Test filter 1"));
  CHECK(info(lfo, "rate") == std::pair<std::string, std::string>("Test LFO 1 Rate", "Test LFO 1"));
}

TEST_CASE_FIXTURE(SlotFixture, "Нумерация сдвигается за удалённым модулем, слот остаётся") {
  const ModuleId first = this->add("test.filter");
  const ModuleId second = this->add("test.filter");

  this->sync();

  const uint32_t slot = this->findSlot(second, "cutoff");
  const SlotTable before = this->table;

  REQUIRE(this->model->apply(RemoveModule{ first }).has_value());
  this->sync();

  REQUIRE(this->table.find(slot) != nullptr);
  CHECK(this->table.find(slot)->name == "Test filter 1 Cutoff");
  CHECK_FALSE(std::ranges::contains(this->table.findReplaced(before), slot));
}

TEST_CASE_FIXTURE(SlotFixture, "Адреса слотов идут в порядке описания") {
  const ModuleId generator = this->add("generator");

  this->sync();

  const std::vector<SlotTarget> targets = this->table.getTargets();

  REQUIRE(targets.size() == SLOT_COUNT);
  CHECK(targets[0] == SlotTarget{ MACROS_MODULE_ID, 0 });
  CHECK(targets[FIRST_POOL_SLOT] == SlotTarget{ generator, 0 });
  CHECK(targets[FIRST_POOL_SLOT + 1] == SlotTarget{ generator, 1 });
  CHECK(targets[FIRST_POOL_SLOT + 2].parameter == NO_PARAMETER);
}

TEST_CASE_FIXTURE(SlotFixture, "Модуль другого типа под тем же ID получает слот заново") {
  const ModuleId filter = this->add("test.filter");

  this->sync();

  // Как при смене пресета: таблица та же, в модели под этим ID другой модуль
  SlotTable restored;

  REQUIRE(restored.restore(FIRST_POOL_SLOT + 10, { filter, "test.lfo", "cutoff" }));
  restored.sync(*this->model);

  CHECK(restored.find(FIRST_POOL_SLOT + 10) == nullptr);
  CHECK(restored.find(FIRST_POOL_SLOT) != nullptr);
}

TEST_CASE_FIXTURE(SlotFixture, "Сохранённая таблица принимает только свободные слоты пула") {
  const ModuleId filter = this->add("test.filter");
  SlotTable restored;

  CHECK_FALSE(restored.restore(0, { filter, "test.filter", "cutoff" }));
  CHECK_FALSE(restored.restore(MACRO_COUNT - 1, { filter, "test.filter", "cutoff" }));
  CHECK_FALSE(restored.restore(SLOT_COUNT, { filter, "test.filter", "cutoff" }));
  CHECK(restored.restore(SLOT_COUNT - 1, { filter, "test.filter", "cutoff" }));
  CHECK_FALSE(restored.restore(SLOT_COUNT - 1, { filter, "test.filter", "drive" }));
}

TEST_CASE_FIXTURE(SlotFixture, "Параметр с двумя слотами в испорченном состоянии оставляет первый") {
  const ModuleId filter = this->add("test.filter");
  SlotTable restored;

  REQUIRE(restored.restore(FIRST_POOL_SLOT + 5, { filter, "test.filter", "cutoff" }));
  REQUIRE(restored.restore(FIRST_POOL_SLOT + 6, { filter, "test.filter", "cutoff" }));

  restored.sync(*this->model);

  CHECK(restored.find(FIRST_POOL_SLOT + 5) != nullptr);
  CHECK(restored.find(FIRST_POOL_SLOT + 6) == nullptr);

  // Иначе лишняя запись заняла бы слот навсегда и ушла в следующее сохранение
  CHECK(restored.getPoolBindings().size() == 2);
}

TEST_CASE_FIXTURE(SlotFixture, "Заглушка слотов не занимает (D-021)") {
  const std::string text = R"({
    "version": 1, "nextModuleId": 5,
    "modules": [
      { "id": 1, "type": "output", "typeVersion": 1 },
      { "id": 2, "type": "unknown.module", "typeVersion": 1, "parameters": { "x": 1 } }
    ],
    "connections": [], "routes": [
      { "source": { "module": 2, "output": "out" }, "target": { "module": 2, "parameter": "x" }, "depth": 0.5 }
    ]
  })";

  const auto loaded = loadModel(&this->registry, text);

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  this->table.sync(*loaded);

  CHECK(countBound(this->table) == MACRO_COUNT);

  // Запись из проекта, где этот модуль ещё был известен, не должна держать слот
  SlotTable restored;

  REQUIRE(restored.restore(FIRST_POOL_SLOT, { 2, "unknown.module", "x" }));
  restored.sync(*loaded);

  CHECK(restored.getPoolBindings().empty());
}

TEST_CASE_FIXTURE(SlotFixture, "Состояние проекта: таблица и имя пресета возвращаются, пресет читается как состояние (D-020)") {
  const ModuleId first = this->add("test.filter");
  const ModuleId second = this->add("test.filter");

  this->sync();
  REQUIRE(this->model->apply(RemoveModule{ first }).has_value());
  this->sync();

  const auto loaded = loadSession(&this->registry, saveSession(*this->model, this->table, "Мой пресет"));

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(loaded->model == *this->model);
  CHECK(loaded->slots == this->table);
  CHECK(loaded->presetName == "Мой пресет");
  CHECK(this->findSlot(second, "cutoff") == FIRST_POOL_SLOT + 2);

  // Старый проект и файл пресета: раздела нет, слоты выдаются заново
  const auto fresh = loadSession(&this->registry, saveModel(*this->model));

  REQUIRE_MESSAGE(fresh.has_value(), fresh.error());
  CHECK(fresh->model == *this->model);
  CHECK(fresh->presetName.empty());
  CHECK(fresh->slots.find(FIRST_POOL_SLOT) != nullptr);
  CHECK(fresh->slots.getPoolBindings().size() == 2);

  // Состояние читается и как обычный пресет
  const auto asPreset = loadModel(&this->registry, saveSession(*this->model, this->table, ""));

  REQUIRE_MESSAGE(asPreset.has_value(), asPreset.error());
  CHECK(*asPreset == *this->model);
}

TEST_CASE_FIXTURE(SlotFixture, "Испорченный раздел session отклоняется целиком") {
  this->add("test.filter");
  this->sync();

  const std::string valid = saveSession(*this->model, this->table, "name");
  const auto replaceAll = [&valid](const std::string& from, const std::string& to) {
    std::string text = valid;
    const size_t position = text.find(from);

    REQUIRE(position != std::string::npos);

    return text.replace(position, from.size(), to);
  };

  CHECK_FALSE(loadSession(&this->registry, replaceAll("\"presetName\": \"name\"", "\"presetName\": 5")).has_value());
  CHECK_FALSE(loadSession(&this->registry, replaceAll("\"slot\": 8", "\"slot\": 3")).has_value());
  CHECK_FALSE(loadSession(&this->registry, replaceAll("\"slot\": 8", "\"slot\": 5000")).has_value());
  CHECK_FALSE(loadSession(&this->registry, replaceAll("\"slot\": 8", "\"slot\": \"x\"")).has_value());
  CHECK_FALSE(loadSession(&this->registry, replaceAll("\"slots\": [", "\"other\": [")).has_value());
}

TEST_CASE("Ящик значений отдаёт только последнее значение каждого слота (D-056)") {
  SlotInbox inbox;
  std::vector<std::pair<uint32_t, float>> received;
  const auto receive = [&received](uint32_t slot, float value) {
    received.emplace_back(slot, value);
  };

  CHECK_FALSE(inbox.takeRequest());
  inbox.drain(receive);
  CHECK(received.empty());

  inbox.post(5, 0.25f);
  inbox.post(5, 0.75f);
  inbox.post(SLOT_COUNT - 1, 0.5f);

  CHECK(inbox.takeRequest());
  CHECK_FALSE(inbox.takeRequest());

  inbox.drain(receive);

  CHECK(received == std::vector<std::pair<uint32_t, float>>{ { 5, 0.75f }, { SLOT_COUNT - 1, 0.5f } });

  received.clear();
  inbox.drain(receive);
  CHECK(received.empty());
}

TEST_CASE_FIXTURE(SlotFixture, "Слот находится по модулю и ключу параметра") {
  const ModuleId filter = this->add("test.filter");

  this->sync();

  const auto slot = this->table.findSlot(filter, "cutoff");

  REQUIRE(slot.has_value());
  CHECK(this->table.find(*slot)->descriptor->key == "cutoff");
  CHECK(this->table.find(*slot)->target.module == filter);
  CHECK(this->table.findSlot(MACROS_MODULE_ID, "macro1") == 0u);
  CHECK_FALSE(this->table.findSlot(filter, "нет такого").has_value());
  CHECK_FALSE(this->table.findSlot(999, "cutoff").has_value());
}

namespace {
  struct OutboxEvent {
    uint32_t slot;
    OUTBOX_EVENT type;
    float value;

    bool operator==(const OutboxEvent&) const = default;
  };

  std::vector<OutboxEvent> drainOutbox(SlotOutbox& outbox) {
    std::vector<OutboxEvent> events;

    outbox.drain([&events](uint32_t slot, OUTBOX_EVENT type, float value) {
      events.push_back({ slot, type, value });
    });

    return events;
  }
}

TEST_CASE("SlotOutbox: движение ручки приходит по порядку, важно только последнее значение (D-067)") {
  SlotOutbox outbox;

  CHECK(drainOutbox(outbox).empty());

  outbox.post(5, 0.1f);
  outbox.beginGesture(5);
  outbox.post(5, 0.2f);
  outbox.post(5, 0.3f);
  outbox.endGesture(5);
  outbox.post(9, 0.7f);

  CHECK(drainOutbox(outbox) == std::vector<OutboxEvent>{
    { 5, OUTBOX_EVENT::GESTURE_BEGIN, 0.0f },
    { 5, OUTBOX_EVENT::VALUE, 0.3f },
    { 5, OUTBOX_EVENT::GESTURE_END, 0.0f },
    { 9, OUTBOX_EVENT::VALUE, 0.7f },
  });

  CHECK(drainOutbox(outbox).empty());
}

TEST_CASE("SlotOutbox: движение, начатое до отправки конца прошлого, не закрывается раньше времени (D-067)") {
  SlotOutbox outbox;

  outbox.beginGesture(5);
  CHECK(drainOutbox(outbox).size() == 1);

  outbox.endGesture(5);
  outbox.beginGesture(5);
  outbox.post(5, 0.4f);

  CHECK(drainOutbox(outbox) == std::vector<OutboxEvent>{ { 5, OUTBOX_EVENT::VALUE, 0.4f } });

  outbox.endGesture(5);

  CHECK(drainOutbox(outbox) == std::vector<OutboxEvent>{ { 5, OUTBOX_EVENT::GESTURE_END, 0.0f } });
}
