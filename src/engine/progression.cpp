#include "engine/progression.h"

namespace engine {

void Progression::set_steps(const std::vector<ProgressionStep>& steps) { steps_ = steps; }

const std::vector<ProgressionStep>& Progression::steps() const { return steps_; }

void Progression::clear() { steps_.clear(); }

}  // namespace engine
