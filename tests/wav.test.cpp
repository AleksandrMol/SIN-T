#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "../tools/wav/wav.h"

namespace {
  std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);

    return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }

  uint32_t readU16(const std::vector<char>& bytes, size_t offset) {
    return (uint8_t)bytes[offset] | ((uint8_t)bytes[offset + 1] << 8);
  }

  uint32_t readU32(const std::vector<char>& bytes, size_t offset) {
    return readU16(bytes, offset) | (readU16(bytes, offset + 2) << 16);
  }
}

TEST_CASE("writeWav пишет заголовок и сэмплы по чередованию каналов") {
  const auto path = std::filesystem::temp_directory_path() / "sint_wav_test.wav";

  const StereoBuffer audio = { { 0.25f, -0.5f, 1.0f }, { 0.75f, 0.0f, -1.0f } };

  REQUIRE(writeWav(path.string(), audio, 48000));

  const std::vector<char> bytes = readFile(path);
  std::filesystem::remove(path);

  const size_t headerSize = 12 + 8 + 18 + 8 + 4 + 8;
  const size_t dataSize = 3 * 2 * 4;

  REQUIRE(bytes.size() == headerSize + dataSize);

  CHECK(std::string(bytes.data(), 4) == "RIFF");
  CHECK(readU32(bytes, 4) == bytes.size() - 8);
  CHECK(std::string(bytes.data() + 8, 8) == "WAVEfmt ");
  CHECK(readU32(bytes, 16) == 18);
  CHECK(readU16(bytes, 20) == 3);
  CHECK(readU16(bytes, 22) == 2);
  CHECK(readU32(bytes, 24) == 48000);
  CHECK(readU32(bytes, 28) == 48000 * 8);
  CHECK(readU16(bytes, 32) == 8);
  CHECK(readU16(bytes, 34) == 32);
  CHECK(std::string(bytes.data() + 38, 4) == "fact");
  CHECK(readU32(bytes, 46) == 3);
  CHECK(std::string(bytes.data() + 50, 4) == "data");
  CHECK(readU32(bytes, 54) == dataSize);

  float samples[6];
  std::memcpy(samples, bytes.data() + headerSize, sizeof(samples));

  CHECK(samples[0] == 0.25f);
  CHECK(samples[1] == 0.75f);
  CHECK(samples[2] == -0.5f);
  CHECK(samples[3] == 0.0f);
  CHECK(samples[4] == 1.0f);
  CHECK(samples[5] == -1.0f);
}

TEST_CASE("writeWav сообщает об ошибке") {
  const auto directory = std::filesystem::temp_directory_path() / "sint_wav_test_missing";

  CHECK_FALSE(writeWav((directory / "out.wav").string(), { { 0.0f }, { 0.0f } }, 48000));
  CHECK_FALSE(writeWav((std::filesystem::temp_directory_path() / "sint_wav_test_unequal.wav").string(), { { 0.0f }, {} }, 48000));
}

TEST_CASE("readWav читает то, что записал writeWav") {
  const auto path = std::filesystem::temp_directory_path() / "sint_wav_test_roundtrip.wav";

  const StereoBuffer audio = { { 0.25f, -0.5f, 1e-30f }, { 0.75f, 0.0f, -1.0f } };

  REQUIRE(writeWav(path.string(), audio, 44100));

  const std::optional<WavFile> wav = readWav(path.string());
  std::filesystem::remove(path);

  REQUIRE(wav.has_value());
  CHECK(wav->sampleRate == 44100);
  CHECK(wav->audio.left == audio.left);
  CHECK(wav->audio.right == audio.right);
}

TEST_CASE("readWav отвергает повреждённые и чужие файлы") {
  const auto path = std::filesystem::temp_directory_path() / "sint_wav_test_broken.wav";

  REQUIRE(writeWav(path.string(), { { 0.25f, 0.5f }, { 0.75f, 1.0f } }, 48000));

  const std::vector<char> valid = readFile(path);
  const size_t headerSize = 12 + 8 + 18 + 8 + 4 + 8;

  const auto readChanged = [&path](const std::vector<char>& bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());

    return readWav(path.string());
  };

  REQUIRE(readChanged(valid).has_value());

  std::vector<char> bytes = valid;

  SUBCASE("Не RIFF") {
    bytes[0] = 'X';
  }

  SUBCASE("Целочисленный формат") {
    bytes[20] = 1;
  }

  SUBCASE("Один канал") {
    bytes[22] = 1;
  }

  SUBCASE("16 бит") {
    bytes[34] = 16;
  }

  SUBCASE("Блок данных длиннее файла") {
    bytes.pop_back();
  }

  SUBCASE("Обрезан заголовок") {
    bytes.resize(30);
  }

  SUBCASE("Нет блока данных") {
    bytes.resize(headerSize - 8);
  }

  SUBCASE("Неполный кадр") {
    bytes[54] = 12;
  }

  SUBCASE("Пустой файл") {
    bytes.clear();
  }

  SUBCASE("Короткий блок формата в конце файла") {
    bytes[16] = 4;
    bytes.resize(12 + 8 + 4);
  }

  SUBCASE("Данные без блока формата") {
    bytes[12] = 'x';
  }

  SUBCASE("Блок нечётной длины в конце файла, байта выравнивания нет") {
    bytes.resize(headerSize - 8);
    bytes.insert(bytes.end(), { 'L', 'I', 'S', 'T', 1, 0, 0, 0, 'x' });
  }

  CHECK_FALSE(readChanged(bytes).has_value());
  std::filesystem::remove(path);
}

TEST_CASE("readWav пропускает чужой блок нечётной длины вместе с байтом выравнивания") {
  const auto path = std::filesystem::temp_directory_path() / "sint_wav_test_extra_chunk.wav";

  REQUIRE(writeWav(path.string(), { { 0.25f }, { 0.75f } }, 48000));

  std::vector<char> bytes = readFile(path);
  const size_t dataChunk = 12 + 8 + 18 + 8 + 4;

  bytes.insert(bytes.begin() + (std::ptrdiff_t)dataChunk, { 'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0 });
  std::ofstream(path, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());

  const std::optional<WavFile> wav = readWav(path.string());
  std::filesystem::remove(path);

  REQUIRE(wav.has_value());
  CHECK(wav->audio.left == std::vector<float>{ 0.25f });
  CHECK(wav->audio.right == std::vector<float>{ 0.75f });
}

TEST_CASE("readWav сообщает об отсутствии файла") {
  CHECK_FALSE(readWav((std::filesystem::temp_directory_path() / "sint_wav_test_absent.wav").string()).has_value());
}
