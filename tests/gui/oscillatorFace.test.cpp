#include <doctest/doctest.h>

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../../gui/parameterControl/parameterControl.h"
#include "../../gui/waveScreen/waveScreen.h"
#include "./editorFixture.h"

struct OscillatorFixture : EditorFixture {
  ModuleId oscillator = 0;

  OscillatorFixture() {
    this->openGenerator();
    this->oscillator = this->findModule("oscillator");
  }

  Rml::Element* arrow(int step) const {
    return this->block(this->oscillator)->QuerySelector("[data-action=\"step-parameter\"][data-step=\"" + std::to_string(step) + "\"]");
  }

  WaveScreen* screen() const {
    return dynamic_cast<WaveScreen*>(this->block(this->oscillator)->QuerySelector("wave-screen"));
  }
};

TEST_CASE_FIXTURE(OscillatorFixture, "У каждого параметра осциллятора ровно один элемент: высота тона и фаза — поля, остальное ручки (D-108)") {
  const std::vector<std::string> fields = { "wave", "octave", "semi", "fine", "coarse", "phase", "rand" };

  for (const ParameterDescriptor& parameter : this->model().findModule(this->oscillator)->descriptor->parameters) {
    CAPTURE(parameter.key);

    const std::string selector = cellSelector(this->oscillator, parameter.key);
    const bool isField = std::ranges::find(fields, parameter.key) != fields.end();

    CHECK(this->count(selector) == 1);
    CHECK(this->knob(this->oscillator, parameter.key)->GetTagName() == (isField ? "value-field" : "knob"));
  }

  CHECK(this->screen() != nullptr);
}

TEST_CASE_FIXTURE(OscillatorFixture, "Поле пишет число коротко, без единиц через пробел (D-108)") {
  CHECK(this->shownValue(this->oscillator, "wave") == "Saw");
  CHECK(this->shownValue(this->oscillator, "octave") == "0");
  CHECK(this->shownValue(this->oscillator, "semi") == "0");
  CHECK(this->shownValue(this->oscillator, "fine") == "0.0");
  CHECK(this->shownValue(this->oscillator, "coarse") == "0.00");
  CHECK(this->shownValue(this->oscillator, "phase") == "0°");
  CHECK(this->shownValue(this->oscillator, "rand") == "100%");

  // У ручки текст прежний, с единицами
  CHECK(this->shownValue(this->oscillator, "detune") == "20.0 ct");

  this->apply(SetParameter{ this->oscillator, "fine", -12.34f });
  CHECK(this->shownValue(this->oscillator, "fine") == "-12.3");
}

TEST_CASE_FIXTURE(OscillatorFixture, "Стрелки переключают волну по кругу, хост видит жест (D-108)") {
  CHECK(this->value(this->oscillator, "wave") == 3.0f);

  this->drainHostEvents();
  this->click(this->arrow(1));

  CHECK(this->value(this->oscillator, "wave") == 4.0f);
  CHECK(this->shownValue(this->oscillator, "wave") == "Triangle");

  const std::vector<SentEvent> events = this->drainHostEvents();

  REQUIRE(events.size() == 3);
  CHECK(events[0].type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
  CHECK(events[1].type == CLAP_EVENT_PARAM_VALUE);
  CHECK(events[2].type == CLAP_EVENT_PARAM_GESTURE_END);

  // После последней волны снова первая, и обратно
  this->click(this->arrow(1));
  CHECK(this->value(this->oscillator, "wave") == 1.0f);

  this->click(this->arrow(-1));
  CHECK(this->value(this->oscillator, "wave") == 4.0f);

  this->click(this->arrow(-1));
  CHECK(this->value(this->oscillator, "wave") == 3.0f);
}

TEST_CASE_FIXTURE(OscillatorFixture, "Поле тянется за подпись и за число, двойной щелчок по числу сбрасывает (D-108)") {
  Rml::Element* semi = this->knob(this->oscillator, "semi");
  const Rml::Vector2f label = this->center(semi->QuerySelector(".field-name"));

  this->dragTo(label, label + Rml::Vector2f(0.0f, -30.0f));

  const float raised = this->value(this->oscillator, "semi");

  CHECK(raised > 0.0f);
  CHECK(raised == std::round(raised));
  CHECK(this->shownValue(this->oscillator, "semi") == std::to_string((int)raised));

  this->doubleClick(semi->QuerySelector(".field-value"));
  CHECK(this->value(this->oscillator, "semi") == 0.0f);
}

TEST_CASE_FIXTURE(OscillatorFixture, "Провод модуляции ложится на поле, поле показывает размах модуляции (D-108)") {
  const ModuleId env = this->findModule("env");
  auto* fine = dynamic_cast<ParameterControl*>(this->knob(this->oscillator, "fine"));

  REQUIRE(fine != nullptr);
  CHECK(fine->getModulationHigh() == fine->getModulationLow());

  this->moveMouse(this->center(this->port(env, "out", "modulation")));
  this->press();
  this->moveMouse(this->center(fine) + Rml::Vector2f(-10.0f, 0.0f));
  this->moveMouse(this->center(fine));

  CHECK(fine->IsClassSet("legal"));

  this->release();

  CHECK(findRoute(this->model(), env, this->oscillator, "fine") != nullptr);
  CHECK_FALSE(fine->IsClassSet("legal"));

  // Элементы после новой связи построены заново
  fine = dynamic_cast<ParameterControl*>(this->knob(this->oscillator, "fine"));
  CHECK(fine->getModulationHigh() > fine->getModulationLow());
}

TEST_CASE_FIXTURE(OscillatorFixture, "Экран рисует волну и сдвиг фазы из параметров без перестройки блока (D-094, D-108)") {
  WaveScreen* screen = this->screen();

  REQUIRE(screen != nullptr);
  CHECK(screen->getType() == WAVE_TYPE::SAW);
  CHECK(screen->getPhaseOffset() == 0.0f);

  this->apply(SetParameter{ this->oscillator, "wave", 1.0f });
  this->apply(SetParameter{ this->oscillator, "phase", 90.0f });

  CHECK(this->screen() == screen);
  CHECK(screen->getType() == WAVE_TYPE::SIN);
  CHECK(screen->getPhaseOffset() == doctest::Approx(0.25f));
}

TEST_CASE_FIXTURE(OscillatorFixture, "Подсветка цели провода видна на каждом поле, и на Wave с пластиной (D-108)") {
  // Пластина рисуется поверх фона: у поля с ней подсветка должна менять слои декоратора, а не только фон
  Rml::ElementList fields;

  this->block(this->oscillator)->QuerySelectorAll(fields, "value-field");
  REQUIRE(fields.size() == 7);

  std::vector<Rml::DecoratorsPtr> decoratorsBefore;
  std::vector<Rml::Colourb> backgrounds;

  for (Rml::Element* field : fields) {
    decoratorsBefore.push_back(this->decorators(field));
    backgrounds.push_back(field->GetComputedValues().background_color());
  }

  const Rml::Vector2f port = this->center(this->port(this->findModule("env"), "out", "modulation"));

  this->moveMouse(port);
  this->press();
  this->moveMouse(port + Rml::Vector2f(40.0f, 40.0f));
  this->moveMouse(port + Rml::Vector2f(80.0f, 80.0f));

  for (size_t index = 0; index < fields.size(); index++) {
    CAPTURE(fields[index]->GetAttribute<Rml::String>("data-parameter", ""));
    REQUIRE(fields[index]->IsClassSet("legal"));

    if (decoratorsBefore[index]) {
      CHECK(this->decorators(fields[index]) != decoratorsBefore[index]);
    } else {
      CHECK(fields[index]->GetComputedValues().background_color() != backgrounds[index]);
    }
  }

  this->release();
}
