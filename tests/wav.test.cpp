#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
