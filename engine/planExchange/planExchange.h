#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

struct ExecutionPlan;

/**
 * Передача планов между главным потоком и аудиопотоком (D-023, D-053). Похоже на атомарную подмену
 * ссылки на неизменяемый объект: главный поток собирает план и кладёт его в почтовый ящик, аудиопоток
 * забирает готовое одной атомарной операцией. Блокировок и выделения памяти в аудиопотоке нет.
 *
 * Память освобождает только главный поток. Планы и всё, что на них ссылается, пронумерованы поколениями,
 * аудиопоток сообщает, какое самое старое поколение он ещё читает, а всё, что старше, можно удалять
 */
class PlanExchange {
  public:
    PlanExchange();
    ~PlanExchange();

    /**
     * Принимает план на хранение и выдаёт ему номер поколения. Вызывается на главном потоке
     */
    ExecutionPlan* adopt(std::unique_ptr<ExecutionPlan> plan);

    /**
     * Кладёт план в почтовый ящик. Если аудиопоток не забрал предыдущий, новый его заменяет.
     * Замену полного состояния при этом потерять нельзя: новый план наследует её признак.
     * Вызывается на главном потоке
     */
    void publish(ExecutionPlan* plan);

    /**
     * Откладывает удаление того, на что могут ссылаться уже собранные планы. Удалится, когда
     * аудиопоток дойдёт до следующего собранного плана. Вызывается на главном потоке
     */
    template <typename T>
    void bury(T item) {
      this->graves.push_back({ this->nextGeneration, std::make_shared<T>(std::move(item)) });
    }

    /**
     * Удаляет всё, что аудиопоток больше не читает. Вызывается на главном потоке
     */
    void collect();

    /**
     * Забирает план из почтового ящика или возвращает nullptr. Вызывается из аудиопотока
     */
    ExecutionPlan* take();

    /**
     * Сообщает самое старое поколение, которое аудиопоток ещё читает. Вызывается из аудиопотока
     */
    void release(uint64_t generation);

  private:
    struct Grave {
      uint64_t generation;
      std::shared_ptr<void> item; // Стирает тип: хоронить можно что угодно
    };

    std::atomic<ExecutionPlan*> pending = nullptr;
    std::atomic<uint64_t> released = 0;
    std::deque<std::unique_ptr<ExecutionPlan>> plans; // От старых к новым
    std::vector<Grave> graves;
    uint64_t nextGeneration = 1;
};
