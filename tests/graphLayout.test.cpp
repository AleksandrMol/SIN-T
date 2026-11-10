#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "../engine/oscillator/oscillatorProcessor.h"
#include "../gui/graphLayout/graphLayout.h"
#include "../gui/wireGeometry/wireGeometry.h"
#include "./modelFixture.h"

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: звук идёт слева направо, выход последний (D-068)") {
  const ModuleId first = this->add("test.osc");
  const ModuleId second = this->add("test.osc");
  const ModuleId filter = this->add("test.filter");
  const ModuleId output = this->model->getOutputId();

  for (const Connection& connection : { Connection{ first, "out", filter, "in" }, Connection{ filter, "out", output, "in" } }) {
    REQUIRE(this->model->apply(Connect{ connection }).has_value());
  }

  const auto positions = computeLayout(*this->model, std::nullopt);

  CHECK(positions.at(first).x < positions.at(filter).x);
  CHECK(positions.at(filter).x < positions.at(output).x);

  // Два источника в одной колонке не накладываются
  CHECK(positions.at(first).x == positions.at(second).x);
  CHECK(positions.at(first).y != positions.at(second).y);
}

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: выход последний, даже если к нему ничего не подключено (D-068)") {
  const ModuleId first = this->add("test.filter");
  const ModuleId second = this->add("test.filter");
  const ModuleId third = this->add("test.filter");

  REQUIRE(this->model->apply(Connect{ { first, "out", second, "in" } }).has_value());
  REQUIRE(this->model->apply(Connect{ { second, "out", third, "in" } }).has_value());

  const auto positions = computeLayout(*this->model, std::nullopt);

  CHECK(positions.at(this->model->getOutputId()).x >= positions.at(third).x);
}

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: колонка считается по самому длинному пути, а не по последнему проводу (D-068)") {
  const ModuleId source = this->add("test.osc");
  const ModuleId middle = this->add("test.filter");
  const ModuleId last = this->add("test.filter");

  REQUIRE(this->model->apply(Connect{ { source, "out", middle, "in" } }).has_value());
  REQUIRE(this->model->apply(Connect{ { middle, "out", last, "in" } }).has_value());
  REQUIRE(this->model->apply(Connect{ { source, "out", last, "in" } }).has_value());

  const auto positions = computeLayout(*this->model, std::nullopt);

  CHECK(positions.at(last).x > positions.at(middle).x);
}

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: модуляторы отдельным рядом снизу, макросов на холсте нет (D-068)") {
  const ModuleId osc = this->add("test.osc");
  const ModuleId first = this->add("test.lfo");
  const ModuleId second = this->add("test.lfo");

  const auto positions = computeLayout(*this->model, std::nullopt);

  CHECK_FALSE(positions.contains(MACROS_MODULE_ID));
  CHECK(positions.at(first).y > positions.at(osc).y);
  CHECK(positions.at(first).y == positions.at(second).y);
  CHECK(positions.at(first).x != positions.at(second).x);
}

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: только модули своего графа (D-063)") {
  const ModuleId generator = this->add("generator");
  const ModuleId inner = this->add("test.osc", generator);
  const ModuleId outer = this->add("test.osc");

  const auto main = computeLayout(*this->model, std::nullopt);
  const auto voice = computeLayout(*this->model, generator);

  CHECK(main.contains(generator));
  CHECK(main.contains(outer));
  CHECK_FALSE(main.contains(inner));

  CHECK(voice.contains(inner));
  CHECK(voice.contains(this->getVoiceOutput(generator)));
  CHECK_FALSE(voice.contains(outer));
  CHECK_FALSE(voice.contains(generator));
}

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: блоки не совпадают и не зависят от вызова (D-068)") {
  for (int index = 0; index < 6; index++) {
    this->add("test.osc");
    this->add("test.lfo");
  }

  const auto positions = computeLayout(*this->model, std::nullopt);

  CHECK(positions == computeLayout(*this->model, std::nullopt));

  for (const auto& [first, a] : positions) {
    for (const auto& [second, b] : positions) {
      if (first != second) {
        CHECK_FALSE(a == b);
      }
    }
  }
}

TEST_CASE_FIXTURE(Fixture, "Авторасстановка: блоки колонки стоят друг под другом по своей высоте, модуляторы ниже всех (D-108)") {
  // Настоящий осциллятор выше прежнего шага колонки в 200 dp: на нём видно, что шаг берётся из высоты блока
  REQUIRE(this->registry.add(&OSCILLATOR_DESCRIPTOR).has_value());

  // В первой колонке осциллятор и над ним ничего: следующий блок колонки должен встать ниже всей его высоты.
  // Во второй колонке блоков больше: модуляторы должны встать ниже самой длинной колонки, а не первой
  const ModuleId output = this->model->getOutputId();
  const ModuleId oscillator = this->add("oscillator");
  const ModuleId source = this->add("test.osc");

  REQUIRE(this->model->apply(Connect{ { oscillator, "out", output, "in" } }).has_value());

  for (int index = 0; index < 4; index++) {
    const ModuleId filter = this->add("test.filter");

    REQUIRE(this->model->apply(Connect{ { source, "out", filter, "in" } }).has_value());
    REQUIRE(this->model->apply(Connect{ { filter, "out", output, "in" } }).has_value());
  }

  const ModuleId lfo = this->add("test.lfo");
  const auto positions = computeLayout(*this->model, std::nullopt);

  REQUIRE(getBlockHeight(*this->model->findModule(oscillator)) > 200.0f);

  // Блоки одной колонки по порядку, каждый ниже предыдущего на его высоту с зазором
  std::map<float, std::vector<ModuleId>> columns;

  for (const auto& [id, position] : positions) {
    if (id != lfo) {
      columns[position.x].push_back(id);
    }
  }

  float bottom = 0.0f;

  for (auto& [x, ids] : columns) {
    std::ranges::sort(ids, [&positions](ModuleId a, ModuleId b) {
      return positions.at(a).y < positions.at(b).y;
    });

    for (size_t index = 0; index < ids.size(); index++) {
      const float top = positions.at(ids[index]).y;
      const float height = getBlockHeight(*this->model->findModule(ids[index]));

      if (index > 0) {
        const ModuleId above = ids[index - 1];

        CHECK(top > positions.at(above).y + getBlockHeight(*this->model->findModule(above)));
      }

      bottom = std::max(bottom, top + height);
    }
  }

  CHECK(columns.size() == 3);
  CHECK(positions.at(lfo).y > bottom);
}

TEST_CASE("Провод начинается и заканчивается в портах, выходит вправо") {
  const WireCurve curve = makeWireCurve({ 10.0f, 20.0f }, { 300.0f, 200.0f });

  CHECK(curve.front() == WirePoint{ 10.0f, 20.0f });
  CHECK(curve.back().x == doctest::Approx(300.0f));
  CHECK(curve.back().y == doctest::Approx(200.0f));
  CHECK(curve[1].x > curve[0].x);
  CHECK(curve[1].y - curve[0].y < curve[1].x - curve[0].x);
}

TEST_CASE("Провод, идущий назад, всё равно отходит от порта вправо") {
  const WireCurve curve = makeWireCurve({ 300.0f, 20.0f }, { 10.0f, 200.0f });

  CHECK(curve[1].x > curve[0].x);
}

TEST_CASE("Клик рядом с проводом определяется по расстоянию") {
  const WireCurve curve = makeWireCurve({ 0.0f, 0.0f }, { 400.0f, 0.0f });

  CHECK(measureDistance(curve, { 200.0f, 0.0f }) < 0.01f);
  CHECK(measureDistance(curve, { 200.0f, 3.0f }) == doctest::Approx(3.0f).epsilon(0.05));
  CHECK(measureDistance(curve, { 200.0f, 100.0f }) > 50.0f);
  CHECK(measureDistance(curve, { -30.0f, 0.0f }) == doctest::Approx(30.0f).epsilon(0.05));
}
