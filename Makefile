# Переменные настроек
CXX = g++
CXXFLAGS = -shared -fPIC -O0 -g -Wall -Wextra -I.
TARGET = $(HOME)/.clap/sint.clap

# Поиск всех файлов .cpp в проекте автоматически
SRCS := $(shell find . -name '*.cpp')

# Главное правило (вызывается по умолчанию при команде make)
all: $(TARGET)

# Правило сборки плагина
$(TARGET): $(SRCS)
	@mkdir -p $(dir $(TARGET))
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)
	@echo "Плагин скомпилирован и сохранен в: $(TARGET)"

# Правило для быстрой очистки скомпилированного файла
clean:
	rm -f $(TARGET)
	@echo "Скомпилированный плагин удален."
