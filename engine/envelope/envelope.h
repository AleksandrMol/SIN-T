#include <cstdint>
#include <functional>
#include <sys/types.h>
#include "../modulation/modulation.h"

enum class ENV_STAGE {
  IDLE,
  ATTACK,
  DECAY,
  SUSTAIN,
  RELEASE
};

struct env_node_t : node_t {
  ENV_STAGE stage;
};

class Envelope : public Modulation<env_node_t> {
  public:
    Envelope();

    void noteOn() override;
    void noteOff() override;
    std::function<void()> onEnd;

    void doSample() override;

  private:
    void doAttack();
    void doRelease();
};
