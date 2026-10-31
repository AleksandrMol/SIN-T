#include "./planExchange.h"

#include <utility>

#include "../executionPlan/executionPlan.h"

PlanExchange::PlanExchange() = default;
PlanExchange::~PlanExchange() = default;

ExecutionPlan* PlanExchange::adopt(std::unique_ptr<ExecutionPlan> plan) {
  plan->generation = this->nextGeneration++;
  this->plans.push_back(std::move(plan));

  return this->plans.back().get();
}

void PlanExchange::publish(ExecutionPlan* plan) {
  const bool isReplacement = plan->isReplacement;
  ExecutionPlan* waiting = this->pending.load(std::memory_order_acquire);

  // План в ящике живёт до release, поэтому читать его признак безопасно. Если аудиопоток успел
  // забрать план между чтением и заменой, замена не пройдёт и признак будет пересчитан
  do {
    plan->isReplacement = isReplacement || (waiting != nullptr && waiting->isReplacement);
  } while (!this->pending.compare_exchange_weak(waiting, plan, std::memory_order_acq_rel, std::memory_order_acquire));
}

void PlanExchange::collect() {
  const uint64_t oldest = this->released.load(std::memory_order_acquire);

  while (!this->plans.empty() && this->plans.front()->generation < oldest) {
    this->plans.pop_front();
  }

  std::erase_if(this->graves, [oldest](const Grave& grave) {
    return grave.generation <= oldest;
  });
}

ExecutionPlan* PlanExchange::take() {
  return this->pending.exchange(nullptr, std::memory_order_acq_rel);
}

void PlanExchange::release(uint64_t generation) {
  this->released.store(generation, std::memory_order_release);
}
