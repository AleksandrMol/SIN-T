#include <charconv>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "../offlineHost/offlineHost.h"

namespace {
  struct RenderOptions {
    std::string path;
    std::vector<int16_t> keys = { 60 };
    double hold = 1.0;
    double tail = 1.5;
    uint32_t sampleRate = 48000;
    uint32_t blockSize = 256;
  };

  template <typename T>
  bool parseNumber(std::string_view text, T& value) {
    const char* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);

    return error == std::errc() && ptr == end;
  }

  /**
   * Разбирает список клавиш MIDI через запятую: 60,64,67
   */
  bool parseKeys(std::string_view text, std::vector<int16_t>& keys) {
    keys.clear();

    while (true) {
      const size_t comma = text.find(',');
      int16_t key = 0;

      if (!parseNumber(text.substr(0, comma), key) || key < 0 || key > 127) {
        return false;
      }

      keys.push_back(key);

      if (comma == std::string_view::npos) {
        return true;
      }

      text.remove_prefix(comma + 1);
    }
  }

  bool parseOptions(int argc, char** argv, RenderOptions& options) {
    if (argc < 2) {
      return false;
    }

    options.path = argv[1];

    for (int i = 2; i + 1 < argc; i += 2) {
      const std::string_view name = argv[i];
      const std::string_view value = argv[i + 1];

      const bool isValid =
        name == "--keys" ? parseKeys(value, options.keys) :
        name == "--hold" ? parseNumber(value, options.hold) :
        name == "--tail" ? parseNumber(value, options.tail) :
        name == "--rate" ? parseNumber(value, options.sampleRate) :
        name == "--block" ? parseNumber(value, options.blockSize) :
        false;

      if (!isValid) {
        return false;
      }
    }

    return argc % 2 == 0
      && options.hold >= 0.0
      && options.tail >= 0.0
      && options.sampleRate > 0
      && options.blockSize > 0;
  }
}

int main(int argc, char** argv) {
  RenderOptions options;

  if (!parseOptions(argc, argv, options)) {
    std::fprintf(stderr,
      "Использование: sint_render <файл.wav> [--keys 60,64,67] [--hold сек] [--tail сек] [--rate Гц] [--block сэмплов]\n");
    return 2;
  }

  const uint32_t holdFrames = (uint32_t)std::lround(options.hold * options.sampleRate);
  const uint32_t tailFrames = (uint32_t)std::lround(options.tail * options.sampleRate);

  std::vector<OfflineNote> notes;
  for (const int16_t key : options.keys) {
    notes.push_back({ .key = key, .startFrame = 0, .endFrame = holdFrames });
  }

  const StereoBuffer audio = renderOffline(notes, {
    .frameCount = holdFrames + tailFrames,
    .sampleRate = options.sampleRate,
    .blockSize = options.blockSize,
  });

  if (audio.left.empty()) {
    std::fprintf(stderr, "Рендер не удался: пустой результат\n");
    return 1;
  }

  if (!writeWav(options.path, audio, options.sampleRate)) {
    std::fprintf(stderr, "Не удалось записать %s\n", options.path.c_str());
    return 1;
  }

  std::printf("%s: %zu сэмплов, %u Гц\n", options.path.c_str(), audio.left.size(), options.sampleRate);

  return 0;
}
