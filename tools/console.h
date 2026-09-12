#include <iostream>

/**
 * Класс для вывода сообщений в консоль
 */
class Console {
  public:
    template <typename... Args>
    void log(Args&&... args) {
      std::cout << "\033[32mLOG >>\033[0m ";

      (std::cout << ... << args);
    
      std::cout << "\n";
    }

  public:
    template <typename... Args>
    void error(Args&&... args) {
      std::cout << "\033[31mERROR >>\033[0m ";

      (std::cout << ... << args);
    
      std::cout << "\n";
    }
};

/**
 * Класс для вывода сообщений в консоль
 */
inline Console console;
