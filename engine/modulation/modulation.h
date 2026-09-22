#include <cstdint>
#include <functional>
#include <sys/types.h>
#include <vector>

enum class MOD_STAGE {
  STOP,
  PROCESS,
};

struct node_t {
  uint32_t time;
  float value;
};

class Modulation {
  public:
    Modulation();
    float currentValue;
    std::function<void()> onEnd;

    void noteOn();
    void noteOff();

    void doSample();

    void setSampleRate(float sampleRate);

    void setNode(uint32_t index, uint32_t time, float value);

  private:
    MOD_STAGE stage;

    std::vector<node_t> nodes;
    uint32_t countOfNodes;
    node_t* currentNode;
    node_t* nextNode;

    uint32_t nodeIndex;
    float nodeStep;
    float nodeTime;

    float sampleRate;
    float samplesPerMillisecond;

    void setStep();
    void setCurrentNode();
    void setNextNode();
    
};