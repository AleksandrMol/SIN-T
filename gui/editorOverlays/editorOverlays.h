#pragma once

#include <RmlUi/Core.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * Пункт меню. Без действия это заголовок
 */
using MenuItems = std::vector<std::pair<std::string, std::function<void()>>>;

/**
 * Всё, что лежит поверх холста: всплывающее меню, ввод текста и сообщение об ошибке.
 * О модели не знает, содержимое и действия приходят снаружи
 */
class EditorOverlays final {
  public:
    /**
     * requestKeyboard вызывается, когда нужен ввод текста: остальное время клавиатура остаётся у DAW
     */
    EditorOverlays(Rml::ElementDocument* document, std::function<void()> requestKeyboard);

    void showMenu(Rml::Vector2f position, MenuItems items);
    void hideMenu();

    /**
     * Скрывает меню, если нажатие пришлось мимо него
     */
    void hideMenuOutside(Rml::Element* target);
    void runMenuItem(size_t index);

    void showPrompt(const std::string& label, const std::string& value, std::function<void(const std::string&)> action);
    void acceptPrompt();
    void hidePrompt();
    bool isPromptOpen() const;

    void showMessage(const std::string& text);

    /**
     * Показанное сообщение. Пусто, если его нет
     */
    std::string getMessage() const;

    /**
     * Скрывает сообщение, время которого вышло. Возвращает true, если что-то скрыто
     */
    bool expireMessage();

  private:
    Rml::ElementDocument* document;
    std::function<void()> requestKeyboard;

    Rml::Element* popup;
    Rml::Element* prompt;
    Rml::Element* promptLabel;
    Rml::Element* promptInput;
    Rml::Element* toast;

    std::vector<std::function<void()>> menuActions;
    std::function<void(const std::string&)> promptAction;
    std::optional<std::chrono::steady_clock::time_point> toastDeadline;
};
