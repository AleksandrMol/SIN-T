#include "./presetLibrary.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

std::optional<std::filesystem::path> findPresetDirectory() {
#ifdef _WIN32
  const char* base = std::getenv("APPDATA");

  if (!base || !*base) {
    return std::nullopt;
  }

  return std::filesystem::path(base) / "SIN-T" / "presets";
#else
  // HOME, а не XDG_DATA_HOME: в flatpak-хосте XDG указывает внутрь песочницы,
  // и у каждого хоста оказались бы свои пресеты
  const char* base = std::getenv("HOME");

  if (!base || !*base) {
    return std::nullopt;
  }

  return std::filesystem::path(base) / ".local" / "share" / "SIN-T" / "presets";
#endif
}

PresetLibrary::PresetLibrary(std::filesystem::path directory) : directory(std::move(directory)) {}

std::vector<std::string> PresetLibrary::list() const {
  std::vector<std::string> names;
  std::error_code error;

  for (const auto& entry : std::filesystem::directory_iterator(this->directory, error)) {
    if (entry.path().extension() == PRESET_EXTENSION) {
      names.push_back(entry.path().stem().string());
    }
  }

  std::ranges::sort(names);

  return names;
}

/**
 * Имя приходит от пользователя и не должно выводить из папки пресетов
 */
std::optional<std::filesystem::path> PresetLibrary::makePath(const std::string& name) const {
  const bool isAllowed = !name.empty()
    && name != "."
    && name != ".."
    && name.find_first_of("/\\:*?\"<>|") == std::string::npos;

  if (!isAllowed) {
    return std::nullopt;
  }

  return this->directory / (name + std::string(PRESET_EXTENSION));
}

std::expected<std::string, std::string> PresetLibrary::read(const std::string& name) const {
  const auto path = this->makePath(name);

  if (!path) {
    return std::unexpected("неверное имя пресета \"" + name + "\"");
  }

  return readTextFile(*path);
}

std::expected<void, std::string> PresetLibrary::write(const std::string& name, std::string_view text, PRESET_WRITE mode) const {
  const auto path = this->makePath(name);

  if (!path) {
    return std::unexpected("неверное имя пресета \"" + name + "\"");
  }

  std::error_code error;

  if (mode == PRESET_WRITE::CREATE && std::filesystem::exists(*path, error)) {
    return std::unexpected("пресет \"" + name + "\" уже есть");
  }

  std::filesystem::create_directories(this->directory, error);

  if (error) {
    return std::unexpected("не удалось создать папку пресетов: " + error.message());
  }

  // Переименование атомарно: на диске всегда лежит либо старый файл целиком, либо новый
  std::filesystem::path temporary = *path;
  temporary += ".tmp";

  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);

    file.write(text.data(), (std::streamsize)text.size());
    file.close();

    if (!file) {
      std::filesystem::remove(temporary, error);

      return std::unexpected("не удалось записать пресет \"" + name + "\"");
    }
  }

  std::filesystem::rename(temporary, *path, error);

  if (error) {
    const std::string message = error.message();

    std::filesystem::remove(temporary, error);

    return std::unexpected("не удалось записать пресет \"" + name + "\": " + message);
  }

  return {};
}

std::expected<std::string, std::string> readTextFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);

  if (!file) {
    return std::unexpected("не удалось открыть файл " + path.string());
  }

  std::ostringstream text;

  text << file.rdbuf();

  if (file.bad()) {
    return std::unexpected("не удалось прочитать файл " + path.string());
  }

  return text.str();
}
