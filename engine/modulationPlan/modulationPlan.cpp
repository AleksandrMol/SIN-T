#include "./modulationPlan.h"

#include <algorithm>

namespace {
  /**
   * Итог D-025: база и сумма вкладов, одно ограничение, затем кривая параметра в реальные единицы
   */
  float calculateReal(const ParameterBinding& parameter) {
    float sum = parameter.base->value;

    for (const RouteBinding& route : parameter.routes) {
      sum += route.depth * *route.source;
    }

    return toReal(*parameter.base->descriptor, sum);
  }

  void updateParameter(const ParameterBinding& parameter) {
    const BaseValue& base = *parameter.base;
    ParameterRamp& ramp = *parameter.ramp;

    // Значение без маршрутов меняется только при сглаживании базы, а пока оно стоит, считать нечего
    if (parameter.routes.empty() && base.value == base.previous && ramp.start == ramp.end) {
      return;
    }

    const float real = calculateReal(parameter);

    // Ступенчатый параметр меняется на границе участка, без интерполяции (D-025)
    ramp.start = base.descriptor->isStepped ? real : ramp.end;
    ramp.end = real;
  }

  void startParameter(const ParameterBinding& parameter) {
    const float real = calculateReal(parameter);

    *parameter.ramp = { real, real };
  }

  void runModulator(const ModulatorBinding& modulator, const VoiceContext& context, uint32_t frames, float tempo) {
    modulator.processor->modulate({
      .frames = frames,
      .parameters = modulator.ramps,
      .voice = &context,
      .tempo = tempo,
      .outputs = modulator.outputs,
    });
  }
}

void runModulators(const VoiceModulation& modulation, const VoiceContext& context, uint32_t frames, float tempo) {
  for (const ModulatorBinding& modulator : modulation.modulators) {
    for (const ParameterBinding& parameter : modulator.parameters) {
      updateParameter(parameter);
    }

    runModulator(modulator, context, frames, tempo);
  }
}

void updateParameters(const VoiceModulation& modulation) {
  for (const ParameterBinding& parameter : modulation.parameters) {
    updateParameter(parameter);
  }
}

void startModulation(const VoiceModulation& modulation, const VoiceContext& context, float tempo) {
  // Значения прошлой ноты не должны попасть в маршруты, замыкающие цикл
  for (const ModulatorBinding& modulator : modulation.modulators) {
    std::fill_n(modulator.outputs, modulator.outputCount, 0.0f);
  }

  for (const ModulatorBinding& modulator : modulation.modulators) {
    for (const ParameterBinding& parameter : modulator.parameters) {
      startParameter(parameter);
    }

    runModulator(modulator, context, 0, tempo);
  }

  for (const ParameterBinding& parameter : modulation.parameters) {
    startParameter(parameter);
  }
}
