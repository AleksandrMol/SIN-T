#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "./modelFixture.h"

TEST_CASE_FIXTURE(Fixture, "Новая модель содержит только выход и макросы") {
  REQUIRE(this->model->getModules().size() == 2);
  REQUIRE(this->model->findModule(MACROS_MODULE_ID) != nullptr);
  CHECK(this->model->findModule(MACROS_MODULE_ID)->descriptor == &MACROS_DESCRIPTOR);

  const ModuleInstance* output = this->model->findModule(this->model->getOutputId());

  REQUIRE(output != nullptr);
  CHECK(output->descriptor == &OUTPUT_DESCRIPTOR);
  CHECK_FALSE(output->owner.has_value());
}

TEST_CASE_FIXTURE(Fixture, "Новый модуль получает уникальный ID и значения по умолчанию") {
  const ModuleId filter = this->add("test.filter");
  const ModuleId other = this->add("test.filter");

  CHECK(filter != other);

  const ModuleInstance* module = this->model->findModule(filter);

  REQUIRE(module != nullptr);
  CHECK(module->parameters.at("cutoff") == 1000.0f);
  CHECK(module->parameters.at("drive") == 0.0f);
}

TEST_CASE_FIXTURE(Fixture, "ID удалённого модуля не используется повторно") {
  const ModuleId first = this->add("test.filter");

  REQUIRE(this->model->apply(RemoveModule{ first }).has_value());

  CHECK(this->add("test.filter") > first);
}

TEST_CASE_FIXTURE(Fixture, "Модуль неизвестного типа не создаётся") {
  this->checkRejected(AddModule{ "test.unknown" });
}

TEST_CASE_FIXTURE(Fixture, "Выход нельзя создать или удалить вручную") {
  const ModuleId generator = this->add("generator");

  this->checkRejected(AddModule{ "output" });
  this->checkRejected(AddModule{ "voiceOutput", generator });
  this->checkRejected(RemoveModule{ this->model->getOutputId() });
  this->checkRejected(RemoveModule{ this->getVoiceOutput(generator) });
}

TEST_CASE_FIXTURE(Fixture, "Генератор создаётся со своим выходом голоса") {
  const ModuleId generator = this->add("generator");
  const ModuleId voiceOutput = this->getVoiceOutput(generator);

  CHECK(this->model->findModule(voiceOutput)->owner == generator);
}

TEST_CASE_FIXTURE(Fixture, "Подграф принимает только допустимые модули") {
  const ModuleId generator = this->add("generator");
  const ModuleId filter = this->add("test.filter");

  CHECK(this->model->findModule(this->add("test.filter", generator))->owner == generator);

  SUBCASE("Модуль, недопустимый в полифонической зоне") {
    this->checkRejected(AddModule{ "test.reverb", generator });
  }

  SUBCASE("Вложенный генератор") {
    this->checkRejected(AddModule{ "generator", generator });
  }

  SUBCASE("Владелец не генератор") {
    this->checkRejected(AddModule{ "test.filter", filter });
  }

  SUBCASE("Владелец не существует") {
    this->checkRejected(AddModule{ "test.filter", 9999 });
  }
}

TEST_CASE_FIXTURE(Fixture, "Схема с разветвлением и объединением") {
  // osc -> filter1 -> output и osc -> filter2 -> output: параллельные цепочки не являются циклом
  const ModuleId osc = this->add("test.osc");
  const ModuleId filter1 = this->add("test.filter");
  const ModuleId filter2 = this->add("test.filter");
  const ModuleId output = this->model->getOutputId();

  CHECK(this->model->apply(wire(osc, filter1)).has_value());
  CHECK(this->model->apply(wire(osc, filter2)).has_value());
  CHECK(this->model->apply(wire(filter1, output)).has_value());
  CHECK(this->model->apply(wire(filter2, output)).has_value());

  CHECK(this->model->getConnections().size() == 4);
}

TEST_CASE_FIXTURE(Fixture, "Некорректные соединения отклоняются") {
  const ModuleId osc = this->add("test.osc");
  const ModuleId filter = this->add("test.filter");

  REQUIRE(this->model->apply(wire(osc, filter)).has_value());

  this->checkRejected(wire(osc, filter));
  this->checkRejected(wire(9999, filter));
  this->checkRejected(wire(osc, 9999));
  this->checkRejected(Connect{ { osc, "missing", filter, "in" } });
  this->checkRejected(Connect{ { osc, "out", filter, "missing" } });
  this->checkRejected(Connect{ { filter, "in", osc, "out" } });
}

TEST_CASE_FIXTURE(Fixture, "Циклы в аудиографе запрещены") {
  const ModuleId a = this->add("test.filter");
  const ModuleId b = this->add("test.filter");
  const ModuleId c = this->add("test.filter");

  REQUIRE(this->model->apply(wire(a, b)).has_value());
  REQUIRE(this->model->apply(wire(b, c)).has_value());

  this->checkRejected(wire(a, a));
  this->checkRejected(wire(b, a));
  this->checkRejected(wire(c, a));
}

TEST_CASE_FIXTURE(Fixture, "Циклы запрещены и внутри подграфа") {
  const ModuleId generator = this->add("generator");
  const ModuleId a = this->add("test.filter", generator);
  const ModuleId b = this->add("test.filter", generator);

  REQUIRE(this->model->apply(wire(a, b)).has_value());

  this->checkRejected(wire(b, a));
}

TEST_CASE_FIXTURE(Fixture, "Соединения работают внутри подграфа, но не между графами") {
  const ModuleId generator = this->add("generator");
  const ModuleId other = this->add("generator");
  const ModuleId osc = this->add("test.osc", generator);
  const ModuleId otherFilter = this->add("test.filter", other);
  const ModuleId mainFilter = this->add("test.filter");

  CHECK(this->model->apply(wire(osc, this->getVoiceOutput(generator))).has_value());
  CHECK(this->model->apply(wire(generator, mainFilter)).has_value());

  this->checkRejected(wire(osc, mainFilter));
  this->checkRejected(wire(osc, otherFilter));
  this->checkRejected(wire(osc, this->model->getOutputId()));
}

TEST_CASE_FIXTURE(Fixture, "Отключение убирает только указанный провод") {
  const ModuleId osc = this->add("test.osc");
  const ModuleId filter1 = this->add("test.filter");
  const ModuleId filter2 = this->add("test.filter");

  REQUIRE(this->model->apply(wire(osc, filter1)).has_value());
  REQUIRE(this->model->apply(wire(osc, filter2)).has_value());

  CHECK(this->model->apply(Disconnect{ wire(osc, filter1).connection }).has_value());

  REQUIRE(this->model->getConnections().size() == 1);
  CHECK(this->model->getConnections()[0].toModule == filter2);

  this->checkRejected(Disconnect{ wire(osc, filter1).connection });
}

TEST_CASE_FIXTURE(Fixture, "После отключения соединение можно создать снова, цикл снимается") {
  const ModuleId a = this->add("test.filter");
  const ModuleId b = this->add("test.filter");

  REQUIRE(this->model->apply(wire(a, b)).has_value());
  REQUIRE(this->model->apply(Disconnect{ wire(a, b).connection }).has_value());

  CHECK(this->model->apply(wire(b, a)).has_value());
}

TEST_CASE_FIXTURE(Fixture, "Удаление модуля удаляет его соединения и маршруты") {
  const ModuleId osc = this->add("test.osc");
  const ModuleId filter = this->add("test.filter");
  const ModuleId lfo = this->add("test.lfo");
  const ModuleId otherFilter = this->add("test.filter");

  REQUIRE(this->model->apply(wire(osc, filter)).has_value());
  REQUIRE(this->model->apply(wire(osc, otherFilter)).has_value());
  REQUIRE(this->model->apply(AddRoute{ route(lfo, filter, "cutoff") }).has_value());
  REQUIRE(this->model->apply(AddRoute{ route(lfo, otherFilter, "cutoff") }).has_value());

  SUBCASE("Удаляется цель") {
    REQUIRE(this->model->apply(RemoveModule{ filter }).has_value());

    CHECK(this->model->findModule(filter) == nullptr);
    REQUIRE(this->model->getConnections().size() == 1);
    CHECK(this->model->getConnections()[0].toModule == otherFilter);
    REQUIRE(this->model->getRoutes().size() == 1);
    CHECK(this->model->getRoutes()[0].target.module == otherFilter);
  }

  SUBCASE("Удаляется источник модуляции") {
    REQUIRE(this->model->apply(RemoveModule{ lfo }).has_value());

    CHECK(this->model->getConnections().size() == 2);
    CHECK(this->model->getRoutes().empty());
  }
}

TEST_CASE_FIXTURE(Fixture, "Удаление генератора удаляет весь подграф и ссылки на него") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->add("test.osc", generator);
  const ModuleId localLfo = this->add("test.lfo", generator);
  const ModuleId globalLfo = this->add("test.lfo");
  const ModuleId filter = this->add("test.filter");

  REQUIRE(this->model->apply(wire(generator, filter)).has_value());
  REQUIRE(this->model->apply(wire(osc, this->getVoiceOutput(generator))).has_value());
  REQUIRE(this->model->apply(AddRoute{ route(globalLfo, osc, "wave") }).has_value());
  REQUIRE(this->model->apply(AddRoute{ route(localLfo, filter, "cutoff") }).has_value());

  const size_t modulesBefore = this->model->getModules().size();

  REQUIRE(this->model->apply(RemoveModule{ generator }).has_value());

  // Генератор, его выход голоса, осциллятор и локальный LFO
  CHECK(this->model->getModules().size() == modulesBefore - 4);
  CHECK(this->model->findModule(osc) == nullptr);
  CHECK(this->model->getConnections().empty());
  CHECK(this->model->getRoutes().empty());
  CHECK(this->model->findModule(globalLfo) != nullptr);
}

TEST_CASE_FIXTURE(Fixture, "Значение параметра хранится в реальных единицах и ограничивается") {
  const ModuleId filter = this->add("test.filter");
  const ModuleId osc = this->add("test.osc");

  const auto getValue = [&](ModuleId id, const std::string& key) {
    return this->model->findModule(id)->parameters.at(key);
  };

  REQUIRE(this->model->apply(SetParameter{ filter, "cutoff", 1200.0f }).has_value());
  CHECK(getValue(filter, "cutoff") == 1200.0f);

  REQUIRE(this->model->apply(SetParameter{ filter, "cutoff", 99999.0f }).has_value());
  CHECK(getValue(filter, "cutoff") == 20000.0f);

  REQUIRE(this->model->apply(SetParameter{ osc, "wave", 2.4f }).has_value());
  CHECK(getValue(osc, "wave") == 2.0f);

  this->checkRejected(SetParameter{ filter, "missing", 1.0f });
  this->checkRejected(SetParameter{ 9999, "cutoff", 1.0f });
  this->checkRejected(SetParameter{ filter, "cutoff", NOT_A_NUMBER });
}

TEST_CASE_FIXTURE(Fixture, "Модуль отключается и включается, связи при этом сохраняются (D-040)") {
  const ModuleId osc = this->add("test.osc");
  const ModuleId filter = this->add("test.filter");
  const ModuleId lfo = this->add("test.lfo");

  CHECK(this->model->findModule(filter)->enabled);

  for (const Command& command : std::initializer_list<Command>{
    wire(osc, filter),
    wire(filter, this->model->getOutputId()),
    AddRoute{ route(lfo, filter, "cutoff") },
    SetParameter{ filter, "cutoff", 1200.0f },
  }) {
    REQUIRE(this->model->apply(command).has_value());
  }

  REQUIRE(this->model->apply(SetModuleEnabled{ filter, false }).has_value());

  CHECK_FALSE(this->model->findModule(filter)->enabled);
  CHECK(this->model->findModule(osc)->enabled);
  CHECK(this->model->getConnections().size() == 2);
  CHECK(this->model->getRoutes().size() == 1);
  CHECK(this->model->findModule(filter)->parameters.at("cutoff") == 1200.0f);

  REQUIRE(this->model->apply(SetParameter{ filter, "cutoff", 500.0f }).has_value());
  REQUIRE(this->model->apply(SetModuleEnabled{ filter, true }).has_value());

  CHECK(this->model->findModule(filter)->enabled);
  CHECK(this->model->findModule(filter)->parameters.at("cutoff") == 500.0f);
}

TEST_CASE_FIXTURE(Fixture, "Отключаются модули подграфа и сам генератор (D-040)") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->add("test.osc", generator);

  REQUIRE(this->model->apply(SetModuleEnabled{ osc, false }).has_value());
  REQUIRE(this->model->apply(SetModuleEnabled{ generator, false }).has_value());

  CHECK_FALSE(this->model->findModule(osc)->enabled);
  CHECK_FALSE(this->model->findModule(generator)->enabled);
}

TEST_CASE_FIXTURE(Fixture, "Выход графа и несуществующий модуль отключить нельзя (D-040)") {
  const ModuleId generator = this->add("generator");
  const ModuleId voiceOutput = this->getVoiceOutput(generator);

  this->checkRejected(SetModuleEnabled{ this->model->getOutputId(), false });
  this->checkRejected(SetModuleEnabled{ voiceOutput, false });
  this->checkRejected(SetModuleEnabled{ 9999, false });

  // Включение выхода ничего не меняет, поэтому допустимо
  CHECK(this->model->apply(SetModuleEnabled{ this->model->getOutputId(), true }).has_value());
  CHECK(this->model->apply(SetModuleEnabled{ voiceOutput, true }).has_value());
}

TEST_CASE_FIXTURE(Fixture, "Правила модуляции между зонами (D-004)") {
  const ModuleId generator = this->add("generator");
  const ModuleId other = this->add("generator");
  const ModuleId localLfo = this->add("test.lfo", generator);
  const ModuleId localFilter = this->add("test.filter", generator);
  const ModuleId otherFilter = this->add("test.filter", other);
  const ModuleId mainLfo = this->add("test.lfo");
  const ModuleId mainFilter = this->add("test.filter");

  SUBCASE("Голосовой модулятор управляет параметром своего генератора") {
    CHECK(this->model->apply(AddRoute{ route(localLfo, localFilter, "cutoff") }).has_value());
  }

  SUBCASE("Голосовой модулятор управляет глобальным параметром") {
    CHECK(this->model->apply(AddRoute{ route(localLfo, mainFilter, "cutoff") }).has_value());
  }

  SUBCASE("Голосовой модулятор не управляет параметром другого генератора") {
    this->checkRejected(AddRoute{ route(localLfo, otherFilter, "cutoff") });
  }

  SUBCASE("Глобальный модулятор управляет любым параметром") {
    CHECK(this->model->apply(AddRoute{ route(mainLfo, localFilter, "cutoff") }).has_value());
    CHECK(this->model->apply(AddRoute{ route(mainLfo, otherFilter, "cutoff") }).has_value());
    CHECK(this->model->apply(AddRoute{ route(mainLfo, mainFilter, "cutoff") }).has_value());
  }
}

TEST_CASE_FIXTURE(Fixture, "Velocity подчиняется правилам голосового модулятора (D-004, D-027)") {
  const ModuleId generator = this->add("generator");
  const ModuleId other = this->add("generator");
  const ModuleId localFilter = this->add("test.filter", generator);
  const ModuleId otherFilter = this->add("test.filter", other);
  const ModuleId mainFilter = this->add("test.filter");

  const auto velocityRoute = [generator](ModuleId target) {
    return AddRoute{ { { generator, "velocity" }, { target, "cutoff" }, 1.0f } };
  };

  CHECK(this->model->apply(velocityRoute(localFilter)).has_value());
  CHECK(this->model->apply(velocityRoute(mainFilter)).has_value());
  this->checkRejected(velocityRoute(otherFilter));
}

TEST_CASE_FIXTURE(Fixture, "Параметры самого генератора относятся к его зоне") {
  // Тестовый генератор с модулируемым параметром: у встроенного таких пока нет
  const ModuleDescriptor generatorWithLevel = [] {
    ModuleDescriptor descriptor = GENERATOR_DESCRIPTOR;
    descriptor.type = "test.generator";
    descriptor.parameters.push_back({
      .key = "level",
      .name = "Level",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 1.0f,
      .isModulatable = true,
    });
    return descriptor;
  }();

  REQUIRE(this->registry.add(&generatorWithLevel).has_value());

  const ModuleId generator = this->add("test.generator");
  const ModuleId other = this->add("test.generator");
  const ModuleId localLfo = this->add("test.lfo", generator);

  CHECK(this->model->apply(AddRoute{ route(localLfo, generator, "level") }).has_value());
  this->checkRejected(AddRoute{ route(localLfo, other, "level") });
}

TEST_CASE_FIXTURE(Fixture, "Некорректные маршруты отклоняются") {
  const ModuleId lfo = this->add("test.lfo");
  const ModuleId filter = this->add("test.filter");

  this->checkRejected(AddRoute{ route(9999, filter, "cutoff") });
  this->checkRejected(AddRoute{ route(lfo, 9999, "cutoff") });
  this->checkRejected(AddRoute{ route(filter, lfo, "rate") });
  this->checkRejected(AddRoute{ route(lfo, filter, "missing") });
  this->checkRejected(AddRoute{ route(lfo, filter, "drive") });
  this->checkRejected(AddRoute{ route(lfo, filter, "cutoff", NOT_A_NUMBER) });

  REQUIRE(this->model->apply(AddRoute{ route(lfo, filter, "cutoff") }).has_value());

  this->checkRejected(AddRoute{ route(lfo, filter, "cutoff", 0.1f) });
}

TEST_CASE_FIXTURE(Fixture, "Один источник управляет несколькими параметрами, у параметра несколько источников") {
  const ModuleId lfo1 = this->add("test.lfo");
  const ModuleId lfo2 = this->add("test.lfo");
  const ModuleId filter1 = this->add("test.filter");
  const ModuleId filter2 = this->add("test.filter");

  CHECK(this->model->apply(AddRoute{ route(lfo1, filter1, "cutoff") }).has_value());
  CHECK(this->model->apply(AddRoute{ route(lfo1, filter2, "cutoff") }).has_value());
  CHECK(this->model->apply(AddRoute{ route(lfo2, filter1, "cutoff") }).has_value());

  const std::vector<ModRoute>& routes = this->model->getRoutes();

  // Порядок создания сохраняется: от него зависит разрыв циклов (D-010)
  REQUIRE(routes.size() == 3);
  CHECK(routes[0].target.module == filter1);
  CHECK(routes[1].target.module == filter2);
  CHECK(routes[2].source.module == lfo2);
}

TEST_CASE_FIXTURE(Fixture, "Циклы модуляции разрешены (D-010)") {
  const ModuleId lfo1 = this->add("test.lfo");
  const ModuleId lfo2 = this->add("test.lfo");

  CHECK(this->model->apply(AddRoute{ route(lfo1, lfo2, "rate") }).has_value());
  CHECK(this->model->apply(AddRoute{ route(lfo2, lfo1, "rate") }).has_value());
  CHECK(this->model->apply(AddRoute{ route(lfo1, lfo1, "rate") }).has_value());
}

TEST_CASE_FIXTURE(Fixture, "Глубина маршрута ограничена -1..1 и меняется командой") {
  const ModuleId lfo = this->add("test.lfo");
  const ModuleId filter = this->add("test.filter");
  const ModSource source = { lfo, "out" };
  const ModTarget target = { filter, "cutoff" };

  REQUIRE(this->model->apply(AddRoute{ route(lfo, filter, "cutoff", 5.0f) }).has_value());
  CHECK(this->model->getRoutes()[0].depth == 1.0f);

  REQUIRE(this->model->apply(SetRouteDepth{ source, target, -0.25f }).has_value());
  CHECK(this->model->getRoutes()[0].depth == -0.25f);

  REQUIRE(this->model->apply(SetRouteDepth{ source, target, -3.0f }).has_value());
  CHECK(this->model->getRoutes()[0].depth == -1.0f);

  this->checkRejected(SetRouteDepth{ source, target, NOT_A_NUMBER });
  this->checkRejected(SetRouteDepth{ { lfo, "missing" }, target, 0.5f });
}

TEST_CASE_FIXTURE(Fixture, "Удаление маршрута") {
  const ModuleId lfo = this->add("test.lfo");
  const ModuleId filter = this->add("test.filter");
  const ModSource source = { lfo, "out" };
  const ModTarget target = { filter, "cutoff" };

  REQUIRE(this->model->apply(AddRoute{ route(lfo, filter, "cutoff") }).has_value());
  REQUIRE(this->model->apply(RemoveRoute{ source, target }).has_value());

  CHECK(this->model->getRoutes().empty());

  this->checkRejected(RemoveRoute{ source, target });
}

TEST_CASE("Переход между моделями: схема держит и старое, и новое") {
  Fixture fixture;
  const ModuleId kept = fixture.add("test.osc");
  const ModuleId removed = fixture.add("test.filter");

  REQUIRE(fixture.model->apply(wire(kept, removed)).has_value());
  REQUIRE(fixture.model->apply(wire(removed, fixture.model->getOutputId())).has_value());

  const SynthModel previous = *fixture.model;
  const ModuleId added = fixture.add("test.filter");

  REQUIRE(fixture.model->apply(RemoveModule{ removed }).has_value());
  REQUIRE(fixture.model->apply(wire(kept, added)).has_value());

  const ModelTransition transition = mergeModels(previous, *fixture.model);

  CHECK(transition.settled == *fixture.model);
  CHECK(transition.scheme.findModule(removed) != nullptr);
  CHECK(transition.scheme.findModule(added) != nullptr);
  CHECK(transition.scheme.getConnections().size() == 3);
}

TEST_CASE("Переход между моделями: модуль исполняется, пока затухает его отключение") {
  Fixture fixture;
  const ModuleId osc = fixture.add("test.osc");
  const SynthModel previous = *fixture.model;

  REQUIRE(fixture.model->apply(SetModuleEnabled{ osc, false }).has_value());

  const ModelTransition switchOff = mergeModels(previous, *fixture.model);
  const ModelTransition switchOn = mergeModels(*fixture.model, previous);

  CHECK(switchOff.scheme.findModule(osc)->enabled);
  CHECK(switchOn.scheme.findModule(osc)->enabled);
  CHECK_FALSE(switchOff.settled.findModule(osc)->enabled);
}

TEST_CASE("Переход между моделями: уходящий маршрут остаётся в схеме, а у оставшегося глубина новая") {
  Fixture fixture;
  const ModuleId lfo = fixture.add("test.lfo");
  const ModuleId other = fixture.add("test.lfo");
  const ModuleId filter = fixture.add("test.filter");
  const ModRoute kept = route(lfo, filter, "cutoff", 0.25f);
  const ModRoute removed = route(other, filter, "cutoff", 0.5f);

  REQUIRE(fixture.model->apply(AddRoute{ kept }).has_value());
  REQUIRE(fixture.model->apply(AddRoute{ removed }).has_value());

  const SynthModel previous = *fixture.model;

  REQUIRE(fixture.model->apply(RemoveRoute{ removed.source, removed.target }).has_value());
  REQUIRE(fixture.model->apply(SetRouteDepth{ kept.source, kept.target, 0.75f }).has_value());

  const std::vector<ModRoute> routes = mergeModels(previous, *fixture.model).scheme.getRoutes();

  REQUIRE(routes.size() == 2);
  CHECK(routes[0].depth == 0.75f);
  CHECK(routes[1] == removed);
}

TEST_CASE("Переход между моделями: маршруты одного источника различаются по цели") {
  Fixture fixture;
  const ModuleId lfo = fixture.add("test.lfo");
  const ModuleId first = fixture.add("test.filter");
  const ModuleId second = fixture.add("test.filter");
  const ModRoute removed = route(lfo, first, "cutoff");

  REQUIRE(fixture.model->apply(AddRoute{ removed }).has_value());
  REQUIRE(fixture.model->apply(AddRoute{ route(lfo, second, "cutoff") }).has_value());

  const SynthModel previous = *fixture.model;

  REQUIRE(fixture.model->apply(RemoveRoute{ removed.source, removed.target }).has_value());

  CHECK(mergeModels(previous, *fixture.model).scheme.getRoutes().size() == 2);
}

TEST_CASE("Переход между моделями: провод, образующий цикл с уходящими, откладывается (D-007)") {
  Fixture fixture;
  const ModuleId first = fixture.add("test.filter");
  const ModuleId second = fixture.add("test.filter");

  REQUIRE(fixture.model->apply(wire(first, second)).has_value());

  const SynthModel previous = *fixture.model;
  const Connection reversed = { second, "out", first, "in" };

  REQUIRE(fixture.model->apply(Disconnect{ { first, "out", second, "in" } }).has_value());
  REQUIRE(fixture.model->apply(Connect{ reversed }).has_value());

  const ModelTransition transition = mergeModels(previous, *fixture.model);

  CHECK(transition.scheme.getConnections() == previous.getConnections());
  CHECK(transition.settled.getConnections().empty());

  // Следующий переход идёт уже от модели без уходящего провода
  const ModelTransition next = mergeModels(transition.settled, *fixture.model);

  CHECK(next.scheme.getConnections() == std::vector<Connection>{ reversed });
  CHECK(next.settled == *fixture.model);
}

TEST_CASE_FIXTURE(Fixture, "Макросы создаются сами, их нельзя создать, удалить и отключить (D-055)") {
  this->checkRejected(AddModule{ "macros" });
  this->checkRejected(RemoveModule{ MACROS_MODULE_ID });
  this->checkRejected(SetModuleEnabled{ MACROS_MODULE_ID, false });

  CHECK(this->model->apply(SetModuleEnabled{ MACROS_MODULE_ID, true }).has_value());
}

TEST_CASE_FIXTURE(Fixture, "Макрос управляет параметром любой зоны (D-011, D-055)") {
  const ModuleId generator = this->add("generator");
  const ModuleId filter = this->add("test.filter", generator);
  const ModuleId lfo = this->add("test.lfo");

  CHECK(this->model->apply(AddRoute{ { { MACROS_MODULE_ID, "macro1" }, { filter, "cutoff" }, 1.0f } }).has_value());
  CHECK(this->model->apply(AddRoute{ { { MACROS_MODULE_ID, "macro8" }, { lfo, "rate" }, 1.0f } }).has_value());
  CHECK_FALSE(this->model->apply(AddRoute{ { { MACROS_MODULE_ID, "macro9" }, { lfo, "rate" }, 1.0f } }).has_value());
}

TEST_CASE_FIXTURE(Fixture, "Значение макроса меняется как обычный параметр и ограничивается (D-055)") {
  REQUIRE(this->model->apply(SetParameter{ MACROS_MODULE_ID, "macro3", 2.0f }).has_value());

  CHECK(this->model->findModule(MACROS_MODULE_ID)->parameters.at("macro3") == 1.0f);
}
