#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * Расширение файлов пресетов (D-020)
 */
inline constexpr std::string_view PRESET_EXTENSION = ".sintpreset";

/**
 * Папка пресетов пользователя (D-020): ~/.local/share/SIN-T/presets на Linux,
 * %APPDATA%\SIN-T\presets на Windows. Пусто, если система не сообщает домашнюю папку
 */
std::optional<std::filesystem::path> findPresetDirectory();

enum class PRESET_WRITE {
  CREATE, // Новый пресет: существующий файл не трогается (ТЗ §9.2)
  OVERWRITE, // Осознанное обновление существующего (ТЗ §9.3)
};

/**
 * Список пресетов и операции с файлами (ТЗ §9.5). Содержимое файлов здесь не разбирается: библиотека
 * работает с текстом, а проверяет его loadModel. Используется только на главном потоке
 */
class PresetLibrary {
  public:
    explicit PresetLibrary(std::filesystem::path directory);

    /**
     * Имена пресетов по алфавиту. Папки ещё может не быть: это пустой список
     */
    std::vector<std::string> list() const;

    std::expected<std::string, std::string> read(const std::string& name) const;

    /**
     * Записывает пресет целиком или никак: сбой посреди записи не портит старый файл
     */
    std::expected<void, std::string> write(const std::string& name, std::string_view text, PRESET_WRITE mode) const;

  private:
    std::filesystem::path directory;

    std::optional<std::filesystem::path> makePath(const std::string& name) const;
};

/**
 * Читает файл целиком. Вызывается на главном потоке
 */
std::expected<std::string, std::string> readTextFile(const std::filesystem::path& path);
