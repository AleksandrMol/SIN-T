#pragma once
#include <concepts>
#include <cstdint>
#include <functional>
#include <vector>

enum class MOD_STAGE {
  STOP,
  PROCESS,
};

struct node_t {
  uint32_t duration;
  float value;
};

template <typename T = node_t>
  requires std::derived_from<T, node_t>
class Modulation {
  public:
    float currentValue = 0.0f;
    std::function<void()> onEnd;

    virtual ~Modulation() = default;

    virtual void noteOn() = 0;
    virtual void noteOff() = 0;
    virtual void doSample() = 0;

    void setSampleRate(float sampleRate) {
      this->sampleRate = sampleRate;
      this->samplesPerMillisecond = sampleRate / 1000.0f;
    }

    float getSamplesPerMillisecond() const {
      return this->samplesPerMillisecond;
    }

  protected:
    std::vector<T> nodes;

    uint32_t nodeIndex = 0;
    float nodeTime = 0.0f;
    float nodeStep = 0.0f;

    float sampleRate = 44100.0f;
    float samplesPerMillisecond = 44.1f;

    void setNodes(std::vector<T> nodes) {
      this->nodes = std::move(nodes);
    }

    T *getCurrentNode() {
      if (this->nodeIndex >= this->nodes.size()) {
        return nullptr;
      }

      return &this->nodes[this->nodeIndex];
    }

    T *getNextNode() {
      if (this->nodeIndex + 1 >= this->nodes.size()) {
        return nullptr;
      }

      return &this->nodes[this->nodeIndex + 1];
    }

    void calculateStep() {
      T *nextNode = getNextNode();

      if (!nextNode || nextNode->duration == 0) {
        this->nodeStep = 0.0f;
        return;
      }

      const float duration = this->samplesPerMillisecond * nextNode->duration;

      this->nodeStep = (nextNode->value - currentValue) / duration;
    }

    void process() {
      T *nextNode = this->getNextNode();

      if (!nextNode) {
          return;
      }

      this->currentValue += this->nodeStep;
      this->nodeTime += 1 / this->samplesPerMillisecond;

      if (this->nodeTime >= nextNode->duration) {
        this->currentValue = nextNode->value;

        this->reset(this->nodeIndex + 1);
      }
    };

    std::vector<T> *getNodes() { 
      return &this->nodes;
    };

    void reset(uint32_t index = 0) {
      this->nodeIndex = index;
      this->nodeTime = 0.0f;

      if (this->nodeIndex >= this->nodes.size()) {
        this->nodeStep = 0.0f;
        return;
      }

      calculateStep();
    }
};
