#include "./engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../generator/generatorProcessor.h"
#include "../modulationCompiler/modulationCompiler.h"

namespace {
  // Сглаживание базового значения (D-025) и переходы между планами (D-023)
  constexpr float SMOOTHING_SECONDS = 0.01f;

  // Ниже этого уровня выход считается тишиной: -100 дБ, как у страховки голоса (D-051)
  constexpr float SILENCE_LEVEL = 1e-5f;

  /**
   * Значения, которые нельзя передать мимо плана: от них зависит устройство плана или память голосов
   */
  bool hasSameStructure(const SynthModel& first, const SynthModel& second) {
    if (first.getConnections() != second.getConnections() || first.getRoutes() != second.getRoutes()) {
      return false;
    }

    return std::ranges::equal(first.getModules(), second.getModules(), [](const auto& left, const auto& right) {
      const ModuleInstance& module = left.second;

      return left.first == right.first
        && module.enabled == right.second.enabled
        && (module.descriptor->role != MODULE_ROLE::GENERATOR
          || module.parameters.at("polyphony") == right.second.parameters.at("polyphony"));
    });
  }

  bool isSilent(const AudioBuffer& buffer) {
    const auto isQuiet = [](float sample) {
      return std::fabs(sample) <= SILENCE_LEVEL;
    };

    return std::ranges::all_of(buffer.left, isQuiet) && std::ranges::all_of(buffer.right, isQuiet);
  }

  void advanceFade(PlanFade& fade, float progress) {
    fade.ramp.start = fade.ramp.end;
    fade.ramp.end = fade.from + (fade.to - fade.from) * progress;
  }
}

void Engine::prepare(float sampleRate) {
  this->sampleRate = sampleRate;

  for (auto& [id, slot] : this->slots) {
    slot.processor->prepare(sampleRate);
  }
}

uint32_t Engine::countFadeChunks() const {
  return (uint32_t)std::max(std::round(SMOOTHING_SECONDS * this->sampleRate / MAX_CHUNK_FRAMES), 1.0f);
}

/**
 * Звука нет, поэтому главный поток работает и за аудиопоток
 */
void Engine::applyDirectly(const SynthModel& model) {
  syncModuleSlots(model, this->slots, this->sampleRate, &this->exchange);
  syncParameterBases(model, this->bases);

  this->setApplied(model);
  this->desired = model;
  this->settling.reset();

  this->switchPlan(this->buildPlan(model));
  this->startMainModulation();
  this->exchange.collect();
}

ExecutionPlan* Engine::buildPlan(const SynthModel& scheme, const TransitionFades& fades) {
  std::unique_ptr<ExecutionPlan> compiled = compilePlan(scheme, this->slots, this->bases, fades);

  compiled->slotTargets = this->slotTargets;

  return this->exchange.adopt(std::move(compiled));
}

void Engine::applyModel(const SynthModel& model) {
  if (!this->isLive) {
    this->applyDirectly(model);
    return;
  }

  // Переходить не от чего, а звук уже может идти
  if (!this->applied) {
    this->replaceModel(model);
    return;
  }

  this->sendParameters(model);
  this->desired = model;

  if (!this->settling) {
    this->startTransition();
  }
}

void Engine::replaceModel(const SynthModel& model) {
  if (!this->isLive) {
    this->applyDirectly(model);
    return;
  }

  // Старые планы ещё читают эти состояния, новое создаётся с нуля
  this->exchange.bury(std::exchange(this->slots, {}));
  this->exchange.bury(std::exchange(this->bases, {}));

  syncModuleSlots(model, this->slots, this->sampleRate, &this->exchange);
  syncParameterBases(model, this->bases);

  this->setApplied(model);
  this->desired = model;
  this->settling.reset();

  ExecutionPlan* next = this->buildPlan(model);

  next->isReplacement = true;
  next->fadeChunks = this->countFadeChunks();
  next->fadeChunksLeft = next->fadeChunks + 1;

  this->exchange.publish(next);
}

void Engine::setLive(bool isLive) {
  if (this->isLive == isLive) {
    return;
  }

  this->isLive = isLive;

  if (isLive) {
    return;
  }

  // Аудиопоток остановился, поэтому его работу можно доделать здесь
  if (this->outgoing) {
    this->finishReplacement();
  }

  if (this->exchange.take() || this->settling) {
    this->applyDirectly(*this->desired);
  }
}

void Engine::update() {
  this->exchange.collect();

  if (!this->settling || this->settledGeneration.load(std::memory_order_acquire) != this->transitionGeneration) {
    return;
  }

  this->setApplied(*std::exchange(this->settling, std::nullopt));

  if (hasSameStructure(*this->applied, *this->desired)) {
    this->publishPlan(*this->applied, {});
  } else {
    this->startTransition();
  }
}

bool Engine::takeUpdateRequest() {
  return this->isUpdateRequested.exchange(false, std::memory_order_relaxed);
}

/**
 * Параметры идут мимо плана. Модуль, которого ещё нет в движке, получит значение при создании
 */
void Engine::sendParameters(const SynthModel& model) {
  for (const auto& [id, module] : model.getModules()) {
    const ModuleInstance* before = this->desired->findModule(id);
    const auto values = this->bases.find(id);

    if (!before || values == this->bases.end()) {
      continue;
    }

    for (BaseValue& base : values->second) {
      const float value = module.parameters.at(base.descriptor->key);

      if (value != before->parameters.at(base.descriptor->key)) {
        base.requested.store(toNormalized(*base.descriptor, value), std::memory_order_relaxed);
        this->hasParameterRequests.store(true, std::memory_order_release);
      }
    }
  }
}

/**
 * Прошлая модель уходит вместе с планами: их базовые значения ссылаются на описания её заглушек
 */
void Engine::setApplied(SynthModel model) {
  if (this->applied) {
    this->exchange.bury(std::move(*this->applied));
  }

  this->applied = std::move(model);
}

void Engine::startTransition() {
  if (hasSameStructure(*this->applied, *this->desired)) {
    return;
  }

  ModelTransition transition = mergeModels(*this->applied, *this->desired);
  const TransitionFades fades = findFades(*this->applied, transition.settled);

  // Затухать нечему, значит исчезает только то, что не звучит
  if (fades.isEmpty()) {
    this->setApplied(std::move(transition.settled));
    this->publishPlan(*this->applied, {});
    return;
  }

  this->publishPlan(transition.scheme, fades);
  this->settling = std::move(transition.settled);
}

void Engine::publishPlan(const SynthModel& scheme, const TransitionFades& fades) {
  syncModuleSlots(scheme, this->slots, this->sampleRate, &this->exchange, SLOT_VALUES::KEEP);
  addParameterBases(scheme, this->bases, this->exchange);

  ExecutionPlan* next = this->buildPlan(scheme, fades);

  if (!fades.isEmpty()) {
    next->fadeChunks = this->countFadeChunks();
    next->fadeChunksLeft = next->fadeChunks + 1;
    this->transitionGeneration = next->generation;
  }

  this->exchange.publish(next);
}

void Engine::reset() {
  if (!this->plan) {
    return;
  }

  if (this->outgoing) {
    this->finishReplacement();
  }

  for (ModuleProcessor* processor : this->plan->main.processors[0]) {
    processor->reset();
  }

  this->pitchBend = 0.0f;
  this->lastNoteGate = false;
  this->pedals = {};
  this->chunkPosition = 0;
  this->startMainModulation();
}

void Engine::takePlan() {
  // Следующая замена ждёт конца текущей: два затухающих плана сразу не нужны
  if (this->outgoing) {
    return;
  }

  ExecutionPlan* next = this->exchange.take();

  if (!next) {
    return;
  }

  if (next->isReplacement) {
    this->startReplacement(next);
  } else {
    this->switchPlan(next);
  }

  this->isUpdateRequested.store(true, std::memory_order_relaxed);
}

void Engine::switchPlan(ExecutionPlan* next) {
  if (this->plan) {
    // Вышедший из плана генератор больше не получит note off. Его голоса уже затихли вместе с проводом
    for (const PlanGenerator& generator : this->plan->generators) {
      const bool isKept = std::ranges::any_of(next->generators, [&generator](const PlanGenerator& other) {
        return other.processor == generator.processor;
      });

      if (!isKept) {
        generator.processor->reset();
      }
    }
  }

  for (const PlanGenerator& generator : next->generators) {
    generator.processor->setLimit(generator.polyphony);
  }

  this->plan = next;
  this->isPlanNew = true;
  this->exchange.release(next->generation);
}

/**
 * Состояния планов не пересекаются, поэтому оба могут работать одновременно. Новая нота сразу идёт
 * в новый план. Если старый молчит, переход не нужен: нота в начале рендера не должна смягчиться
 */
void Engine::startReplacement(ExecutionPlan* next) {
  if (this->plan && !isSilent(*this->plan->main.output)) {
    this->outgoing = this->plan;
  } else {
    next->fadeChunksLeft = 0;
  }

  for (const PlanGenerator& generator : next->generators) {
    generator.processor->setLimit(generator.polyphony);
  }

  this->plan = next;
  this->isPlanNew = true;
  this->startMainModulation();
  this->exchange.release((this->outgoing ? this->outgoing : next)->generation);
}

void Engine::finishReplacement() {
  this->outgoing = nullptr;
  this->exchange.release(this->plan->generation);
  this->isUpdateRequested.store(true, std::memory_order_relaxed);
}

/**
 * Значения главного графа принимаются сразу: иначе первый шаг сетки скользил бы от устаревших
 */
void Engine::startMainModulation() {
  startModulation(this->plan->main.modulation[0], this->makeContext(), this->tempo);
}

void Engine::noteOn(const NoteAddress& note, float velocity) {
  // Нота в начале рендера не должна попасть в план, который заменяет загруженный пресет
  if (this->chunkPosition == 0) {
    this->takePlan();
  }

  if (!this->plan) {
    return;
  }

  const ModulationClock clock = { this->tempo, this->pitchBend };

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->noteOn(note, velocity, generator, this->chunkPosition, clock);
  }

  // Глобальный экземпляр общего модулятора перезапускает каждая нота (D-005)
  for (ModuleProcessor* modulator : this->plan->sharedModulators) {
    modulator->reset();
  }

  this->lastNote = note;
  this->lastNoteGate = true;
  this->isLastNoteSustained = false;
}

void Engine::releaseLastNote(const NoteAddress& note) {
  if (matchesNote(note, this->lastNote)) {
    this->lastNoteGate = false;
  }
}

void Engine::noteOff(const NoteAddress& note) {
  if (!this->plan) {
    return;
  }

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->noteOff(note, this->pedals);
  }

  if (isPedalDown(this->pedals, this->lastNote.channel)) {
    this->isLastNoteSustained = this->isLastNoteSustained || matchesNote(note, this->lastNote);
  } else {
    this->releaseLastNote(note);
  }
}

void Engine::choke(const NoteAddress& note) {
  if (!this->plan) {
    return;
  }

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->choke(note);
  }

  this->releaseLastNote(note);
}

void Engine::setSustain(int16_t channel, bool isDown) {
  this->pedals[(size_t)channel] = isDown;

  if (isDown || !this->plan) {
    return;
  }

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->releaseSustained(channel);
  }

  if (this->isLastNoteSustained && this->lastNote.channel == channel) {
    this->isLastNoteSustained = false;
    this->lastNoteGate = false;
  }
}

void Engine::allNotesOff(int16_t channel) {
  this->setSustain(channel, false);
  this->noteOff({ -1, -1, channel, -1 });
}

void Engine::allSoundOff(int16_t channel) {
  this->setSustain(channel, false);
  this->choke({ -1, -1, channel, -1 });
}

void Engine::setPitchBend(float value) {
  this->pitchBend = value;
}

void Engine::setTempo(float tempo) {
  this->tempo = tempo > 0.0f ? tempo : DEFAULT_TEMPO;
}

BaseValue* Engine::findBase(ModuleId module, uint32_t parameter) {
  if (!this->plan) {
    return nullptr;
  }

  const auto values = this->plan->bases.find(module);

  if (values == this->plan->bases.end() || parameter >= values->second.size()) {
    return nullptr;
  }

  return &values->second[parameter];
}

void Engine::setBase(BaseValue& base, float normalized) {
  // Иначе следующий просмотр запросов вернул бы старое значение от главного потока
  base.requested.store(normalized, std::memory_order_relaxed);
  this->smoothBase(base, normalized);
}

void Engine::setParameter(ModuleId module, uint32_t parameter, float value) {
  if (BaseValue* base = this->findBase(module, parameter)) {
    this->setBase(*base, toNormalized(*base->descriptor, value));
  }
}

void Engine::setSlot(uint32_t slot, float normalized) {
  if (!this->plan || slot >= this->plan->slotTargets.size()) {
    return;
  }

  const SlotTarget& target = this->plan->slotTargets[slot];

  if (BaseValue* base = this->findBase(target.module, target.parameter)) {
    this->setBase(*base, normalized);
  }
}

void Engine::setSlotTargets(std::vector<SlotTarget> targets) {
  this->slotTargets = std::move(targets);
}

void Engine::acknowledgeParameter(const SetParameter& command) {
  // Модуля там уже может не быть: значение могло обогнать его удаление
  if (this->desired) {
    (void)this->desired->apply(command);
  }
}

void Engine::smoothBase(BaseValue& base, float target) {
  base.target = target;

  // Промежуточные ступени были бы слышны: тип волны прошёл бы через все волны между старой и новой
  base.step = base.descriptor->isStepped ? 1.0f : std::fabs(base.target - base.value) / (float)this->countFadeChunks();

  this->isSmoothing = true;
}

void Engine::readParameterRequests() {
  for (auto& [id, values] : this->plan->bases) {
    for (BaseValue& base : values) {
      const float requested = base.requested.load(std::memory_order_relaxed);

      if (requested != base.target) {
        this->smoothBase(base, requested);
      }
    }
  }
}

void Engine::advanceBases() {
  if (!this->isSmoothing) {
    return;
  }

  this->isSmoothing = false;

  for (auto& [id, values] : this->plan->bases) {
    for (BaseValue& base : values) {
      base.previous = base.value;

      if (base.value != base.target) {
        const float distance = base.target - base.value;

        base.value = std::fabs(distance) <= base.step ? base.target : base.value + std::copysign(base.step, distance);

        this->isSmoothing = true;
      }
    }
  }
}

VoiceContext Engine::makeContext() const {
  return {
    .pitch = REFERENCE_PITCH,
    .velocity = 1.0f,
    .pitchBend = this->pitchBend,
    .gate = this->lastNoteGate,
  };
}

/**
 * Считает модуляцию на следующий шаг сетки. Голоса идут после глобальных модуляторов и видят их текущие
 * значения, а глобальные модуляторы видят значения голосов с прошлого шага. Параметры главного графа
 * считаются последними и видят всё текущее (D-009, D-010)
 */
void Engine::modulate(ExecutionPlan& target, PARAMETER_UPDATE update) {
  const VoiceModulation& modulation = target.main.modulation[0];
  const ModulationClock clock = { this->tempo, this->pitchBend };

  runModulators(modulation, this->makeContext(), MAX_CHUNK_FRAMES, this->tempo, update);

  for (const PlanGenerator& generator : target.generators) {
    generator.processor->modulateVoices(generator, clock, update);
  }

  updateParameters(modulation, update);
}

/**
 * Переход идёт по сетке: один лишний шаг нужен, чтобы последний участок значений стал ровным.
 * Если план заменили посреди перехода, он доходит его сам: остановленный участок повторялся бы каждый шаг
 */
void Engine::advanceFades(ExecutionPlan& target) {
  if (target.fadeChunksLeft == 0) {
    return;
  }

  target.fadeChunksLeft--;

  const float progress = std::min((float)(target.fadeChunks + 1 - target.fadeChunksLeft) / (float)target.fadeChunks, 1.0f);

  advanceFade(target.outputFade, progress);

  for (auto& [key, fade] : target.wireFades) {
    advanceFade(fade, progress);
  }

  for (auto& [key, fade] : target.moduleFades) {
    advanceFade(fade, progress);
  }

  for (auto& [key, fade] : target.routeFades) {
    advanceFade(fade, progress);
  }
}

/**
 * Сообщает главному потоку и заканчивает замену, когда переход текущего плана окончен
 */
void Engine::advanceCurrentFades() {
  if (this->plan->fadeChunksLeft == 0) {
    return;
  }

  this->advanceFades(*this->plan);

  if (this->plan->fadeChunksLeft == 0) {
    if (this->outgoing) {
      this->finishReplacement();
    }

    this->settledGeneration.store(this->plan->generation, std::memory_order_release);
    this->isUpdateRequested.store(true, std::memory_order_relaxed);
  }
}

void Engine::beginChunk(bool hasRequests) {
  if (hasRequests) {
    this->readParameterRequests();
  }

  this->advanceBases();

  if (this->outgoing) {
    this->advanceFades(*this->outgoing);
  }

  this->advanceCurrentFades();
  this->modulate(*this->plan, this->isPlanNew ? PARAMETER_UPDATE::ALL : PARAMETER_UPDATE::CHANGED);

  if (this->outgoing) {
    this->modulate(*this->outgoing, PARAMETER_UPDATE::CHANGED);
  }

  this->isPlanNew = false;
}

void Engine::writeOutput(float* left, float* right, uint32_t frames) const {
  const AudioBuffer& output = *this->plan->main.output;

  if (!this->outgoing) {
    std::copy_n(output.left.begin(), frames, left);
    std::copy_n(output.right.begin(), frames, right);
    return;
  }

  const AudioBuffer& old = *this->outgoing->main.output;
  const ParameterRamp& fade = this->plan->outputFade.ramp;

  for (uint32_t frame = 0; frame < frames; frame++) {
    const float gain = fade.at(this->chunkPosition + frame, MAX_CHUNK_FRAMES);

    left[frame] = output.left[frame] * gain + old.left[frame] * (1.0f - gain);
    right[frame] = output.right[frame] * gain + old.right[frame] * (1.0f - gain);
  }
}

void Engine::process(float* left, float* right, uint32_t frames) {
  if (!this->plan) {
    this->takePlan();
  }

  if (!this->plan) {
    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);
    return;
  }

  uint32_t done = 0;

  while (done < frames) {
    if (this->chunkPosition == 0) {
      // Флаг читается до плана. Главный поток публикует план раньше запроса к его модулям,
      // поэтому увиденный запрос гарантирует, что план уже в ящике. В обратном порядке
      // запрос мог бы просмотреть старый план и потеряться
      const bool hasRequests = this->hasParameterRequests.exchange(false, std::memory_order_acquire);

      this->takePlan();
      this->beginChunk(hasRequests);
    }

    const uint32_t count = std::min(MAX_CHUNK_FRAMES - this->chunkPosition, frames - done);
    const ChunkSpan span = { count, this->chunkPosition, MAX_CHUNK_FRAMES, this->chunkPosition };

    runGraph(this->plan->main, 0, span, this->makeContext());

    if (this->outgoing) {
      runGraph(this->outgoing->main, 0, span, this->makeContext());
    }

    this->writeOutput(left + done, right + done, count);

    done += count;
    this->chunkPosition = (this->chunkPosition + count) % MAX_CHUNK_FRAMES;
  }
}
