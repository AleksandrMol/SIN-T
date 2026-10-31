#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../engine/presetLibrary/presetLibrary.h"

namespace {
  /**
   * Своя пустая папка на каждый тест
   */
  struct LibraryFixture {
    std::filesystem::path directory;
    PresetLibrary library;

    LibraryFixture()
      : directory(std::filesystem::temp_directory_path()
          / ("sint-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
          / "presets"),
        library(directory) {}

    ~LibraryFixture() {
      std::filesystem::remove_all(this->directory.parent_path());
    }
  };
}

TEST_CASE_FIXTURE(LibraryFixture, "Папки ещё нет: список пуст, первая запись её создаёт") {
  CHECK(this->library.list().empty());
  CHECK_FALSE(this->library.read("нет такого").has_value());

  REQUIRE(this->library.write("Первый", "текст", PRESET_WRITE::CREATE).has_value());

  CHECK(this->library.list() == std::vector<std::string>{ "Первый" });
  CHECK(*this->library.read("Первый") == "текст");
  CHECK(std::filesystem::exists(this->directory / "Первый.sintpreset"));
}

TEST_CASE_FIXTURE(LibraryFixture, "Список идёт по алфавиту и содержит только пресеты") {
  REQUIRE(this->library.write("b", "1", PRESET_WRITE::CREATE).has_value());
  REQUIRE(this->library.write("a", "2", PRESET_WRITE::CREATE).has_value());

  std::ofstream(this->directory / "notes.txt") << "не пресет";

  CHECK(this->library.list() == std::vector<std::string>{ "a", "b" });
}

TEST_CASE_FIXTURE(LibraryFixture, "Пресет не перезаписывается без явной просьбы (ТЗ §9.2, §9.3)") {
  REQUIRE(this->library.write("a", "старый", PRESET_WRITE::CREATE).has_value());

  CHECK_FALSE(this->library.write("a", "новый", PRESET_WRITE::CREATE).has_value());
  CHECK(*this->library.read("a") == "старый");

  REQUIRE(this->library.write("a", "новый", PRESET_WRITE::OVERWRITE).has_value());
  CHECK(*this->library.read("a") == "новый");
}

TEST_CASE_FIXTURE(LibraryFixture, "После записи не остаётся временных файлов") {
  REQUIRE(this->library.write("a", "текст", PRESET_WRITE::CREATE).has_value());

  size_t files = 0;

  for (const auto& entry : std::filesystem::directory_iterator(this->directory)) {
    files += entry.is_regular_file() ? 1 : 0;
  }

  CHECK(files == 1);
}

TEST_CASE_FIXTURE(LibraryFixture, "Имя не может вывести из папки пресетов") {
  for (const char* name : { "", ".", "..", "../outside", "a/b", "a\\b", "c:x", "a*", "a?" }) {
    CAPTURE(name);
    CHECK_FALSE(this->library.write(name, "текст", PRESET_WRITE::OVERWRITE).has_value());
    CHECK_FALSE(this->library.read(name).has_value());
  }

  CHECK_FALSE(std::filesystem::exists(this->directory.parent_path() / "outside.sintpreset"));
}

TEST_CASE_FIXTURE(LibraryFixture, "Запись в недоступное место возвращает ошибку") {
  std::filesystem::create_directories(this->directory.parent_path());
  std::ofstream(this->directory) << "файл вместо папки";

  CHECK_FALSE(this->library.write("a", "текст", PRESET_WRITE::OVERWRITE).has_value());
}

#ifndef _WIN32
TEST_CASE("Папка пресетов строится от домашней папки, а не от XDG (D-020)") {
  const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";

  setenv("HOME", "/home/test", 1);
  setenv("XDG_DATA_HOME", "/sandbox/data", 1);

  const auto directory = findPresetDirectory();

  setenv("HOME", "", 1);
  CHECK_FALSE(findPresetDirectory().has_value());

  setenv("HOME", home.c_str(), 1);
  unsetenv("XDG_DATA_HOME");

  REQUIRE(directory.has_value());
  CHECK(*directory == std::filesystem::path("/home/test/.local/share/SIN-T/presets"));
}
#endif
