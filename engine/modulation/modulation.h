#include <cstdint>
#include <functional>
#include <vector>
#include <concepts>

#include "../../tools/console.h"

enum class MOD_STAGE {
  STOP,
  PROCESS,
};

struct node_t {
  uint32_t time;
  float value;
};

template<typename T = node_t>
  requires std::derived_from<T, node_t>
class Modulation {

  public:
    Modulation() {
      this->currentValue = 0.0f;

      this->nodeIndex = 0;
      this->nodeStep = 0;
      this->nodeTime = 0;
    };

    float currentValue;
    std::function<void()> onEnd;

    virtual void noteOn() = 0;
    virtual void noteOff() = 0;

    virtual void doSample() = 0;

    void setSampleRate(float sampleRate) {
      this->sampleRate = sampleRate;
      this->samplesPerMillisecond = sampleRate / 1000;
    };
    float getSamplesPerMillisecond() {
      return this->samplesPerMillisecond;
    };

  protected:
    std::vector<T> nodes;

    T* currentNode;
    T* nextNode;

    uint32_t nodeIndex;
    float nodeStep;
    float nodeTime;

    float sampleRate;
    float samplesPerMillisecond;

    void setStep() {
      if (this->nodeIndex >= this->nodes.size()) {
        this->nodeStep = 0;
        return;
      }

      console.log("this->nodeIndex ", this->nodeIndex);
      // console.log("this->nodes.size() ", this->nodes.size());
      // console.log("this->currentValue ", this->currentValue);
      console.log("this->nextNode->value ", this->nextNode->value);
      // console.log("this->samplesPerMillisecond ", this->samplesPerMillisecond);
      // console.log("this->nextNode->time ", this->nextNode->time);
      const float step = -1 * ((this->currentValue-this->nextNode->value) / (this->samplesPerMillisecond * this->nextNode->time));
      console.log("STEP ", step);
      this->nodeStep = step;
    };

    void process() {
      this->currentValue += this->nodeStep;
      this->nodeTime += 1/this->samplesPerMillisecond;

      if (this->nodeTime >= this->nextNode->time) {
        this->currentValue = this->nextNode->value;
        this->nodeIndex ++;
        this->setCurrentNode();
        this->setNextNode();
        this->setStep();
      }
    };

    void setNodes(std::vector<T> nodes) {
      this->nodes = nodes;
    };
    std::vector<T>* getNodes() {
      return &this->nodes;
    };

    void setCurrentNode() {
      this->nodeTime = 0;
      this->currentNode = &this->nodes.at(this->nodeIndex);
    };
    T* getCurrentNode() {
      return this->currentNode;
    };

    void setNextNode() {
      this->nextNode = &this->nodes.at(this->nodeIndex + 1);
    };
    T* getNextNode() {
      return this->nextNode;
    };

};
