#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class EditorBackend;

/**
 * Окно редактора внутри окна хоста: pugl отвечает за окно и ввод, RmlUi за содержимое, отрисовка через OpenGL.
 * События окна приходят, только пока хост крутит tick. Перерисовка идёт только при изменениях.
 * Вызывается на главном потоке
 */
class PluginWindow {
  public:
    explicit PluginWindow(EditorBackend& backend);
    ~PluginWindow();

    /**
     * Встраивает окно в окно хоста. Парный вызов возможен только один на объект
     */
    bool attach(uintptr_t parent);

    bool show();
    bool hide();

    /**
     * Размер в пикселях экрана
     */
    uint32_t getWidth() const;
    uint32_t getHeight() const;
    bool setSize(uint32_t width, uint32_t height);

    /**
     * Масштаб экрана: 1, 1.5, 2. Вызывается до attach или после, размеры окна хост задаёт сам
     */
    void setScale(double scale);

    /**
     * Обрабатывает накопившиеся события и при необходимости перерисовывает окно
     */
    void tick();

    /**
     * Таймер окна для хоста без clap.timer-support. На Windows события окна идут через цикл самого хоста,
     * поэтому tick не нужен. На Linux без таймера хоста событий не будет
     */
    void startOwnTimer();

    /**
     * Сохраняет следующий кадр в файл PPM: так внешний вид проверяется без DAW
     */
    void captureFrame(std::string path);

    /**
     * Сколько кадров нарисовано. Окно рисует только при изменениях, и так это проверяется
     */
    uint64_t getFrameCount() const;

    /**
     * Показывает подграф генератора. Пусто — главный граф
     */
    void openGraph(std::optional<uint32_t> generator);

  private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation;
};
