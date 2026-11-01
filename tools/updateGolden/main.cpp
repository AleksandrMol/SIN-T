#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

#include "../goldenScenarios/goldenScenarios.h"

/*
  Обновляет эталоны звука (D-072). Запускать только при намеренном изменении звука:
  cmake --build build --target update_golden
*/
namespace {
  /**
   * Файл переписывается, только если звук вышел за допуск: иначе в истории git копился бы
   * двоичный шум от разницы между машинами
   */
  bool updateScenario(const GoldenScenario& scenario, const std::filesystem::path& sourceDirectory) {
    const auto rendered = renderGolden(scenario, sourceDirectory);

    if (!rendered) {
      std::fprintf(stderr, "%s: %s\n", scenario.name.c_str(), rendered.error().c_str());
      return false;
    }

    if (!isAudible(rendered->audio)) {
      std::fprintf(stderr, "%s: рендер тихий или содержит нечисловые сэмплы, эталон не записан\n", scenario.name.c_str());
      return false;
    }

    const std::filesystem::path path = getGoldenPath(scenario, sourceDirectory);
    const std::optional<WavFile> golden = readWav(path.string());

    if (golden) {
      const GoldenDifference difference = compareGolden(*golden, *rendered);

      if (difference.amount <= GOLDEN_TOLERANCE) {
        std::printf("%s: в пределах допуска (%g), файл не менялся\n", scenario.name.c_str(), difference.amount);
        return true;
      }

      if (difference.amount == std::numeric_limits<float>::infinity()) {
        std::printf("%s: другая длина или частота дискретизации\n", scenario.name.c_str());
      } else {
        const double seconds = (double)difference.frame / scenario.sampleRate;

        std::printf("%s: разница %g на %.3f с\n", scenario.name.c_str(), difference.amount, seconds);
      }
    } else {
      std::printf("%s: эталона ещё не было\n", scenario.name.c_str());
    }

    if (!writeWav(path.string(), rendered->audio, scenario.sampleRate)) {
      std::fprintf(stderr, "Не удалось записать %s\n", path.string().c_str());
      return false;
    }

    std::printf("%s: эталон обновлён\n", scenario.name.c_str());

    return true;
  }
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "Использование: sint_update_golden <корень проекта>\n");
    return 2;
  }

  bool isDone = true;

  for (const GoldenScenario& scenario : getGoldenScenarios()) {
    isDone = updateScenario(scenario, argv[1]) && isDone;
  }

  return isDone ? 0 : 1;
}
