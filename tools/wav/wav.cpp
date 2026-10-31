#include "./wav.h"

#include <bit>
#include <cstddef>
#include <fstream>

namespace {
  constexpr uint16_t FORMAT_IEEE_FLOAT = 3;
  constexpr uint16_t CHANNEL_COUNT = 2;
  constexpr uint16_t BYTES_PER_SAMPLE = 4;

  // Для форматов, отличных от PCM, спецификация WAV требует блок fmt из 18 байт и блок fact
  constexpr uint32_t FMT_CHUNK_SIZE = 18;
  constexpr uint32_t FACT_CHUNK_SIZE = 4;
  constexpr uint32_t CHUNK_HEADER_SIZE = 8;

  void writeU16(std::vector<char>& bytes, uint16_t value) {
    bytes.push_back((char)(value & 0xFF));
    bytes.push_back((char)(value >> 8));
  }

  void writeU32(std::vector<char>& bytes, uint32_t value) {
    writeU16(bytes, (uint16_t)(value & 0xFFFF));
    writeU16(bytes, (uint16_t)(value >> 16));
  }

  void writeTag(std::vector<char>& bytes, const char (&tag)[5]) {
    bytes.insert(bytes.end(), tag, tag + 4);
  }
}

bool writeWav(const std::string& path, const StereoBuffer& audio, uint32_t sampleRate) {
  if (audio.left.size() != audio.right.size()) {
    return false;
  }

  const uint32_t frameCount = (uint32_t)audio.left.size();
  const uint16_t frameSize = CHANNEL_COUNT * BYTES_PER_SAMPLE;
  const uint32_t dataSize = frameCount * frameSize;
  const uint32_t riffSize = 4
    + CHUNK_HEADER_SIZE + FMT_CHUNK_SIZE
    + CHUNK_HEADER_SIZE + FACT_CHUNK_SIZE
    + CHUNK_HEADER_SIZE + dataSize;

  std::vector<char> bytes;
  bytes.reserve(riffSize + CHUNK_HEADER_SIZE);

  writeTag(bytes, "RIFF");
  writeU32(bytes, riffSize);
  writeTag(bytes, "WAVE");

  writeTag(bytes, "fmt ");
  writeU32(bytes, FMT_CHUNK_SIZE);
  writeU16(bytes, FORMAT_IEEE_FLOAT);
  writeU16(bytes, CHANNEL_COUNT);
  writeU32(bytes, sampleRate);
  writeU32(bytes, sampleRate * frameSize);
  writeU16(bytes, frameSize);
  writeU16(bytes, BYTES_PER_SAMPLE * 8);
  writeU16(bytes, 0);

  writeTag(bytes, "fact");
  writeU32(bytes, FACT_CHUNK_SIZE);
  writeU32(bytes, frameCount);

  writeTag(bytes, "data");
  writeU32(bytes, dataSize);

  for (size_t i = 0; i < frameCount; i++) {
    writeU32(bytes, std::bit_cast<uint32_t>(audio.left[i]));
    writeU32(bytes, std::bit_cast<uint32_t>(audio.right[i]));
  }

  std::ofstream file(path, std::ios::binary);
  file.write(bytes.data(), (std::streamsize)bytes.size());

  return file.good();
}
