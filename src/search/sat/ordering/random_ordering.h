#ifndef SAT_ORDERING_RANDOM_ORDERING_H
#define SAT_ORDERING_RANDOM_ORDERING_H

#include "action_ordering.h"

namespace sat_search {

// Operators are ordered randomly.
class RandomOrdering : public ActionOrdering {
	const int random_seed;
protected:
	ActionOrderingResult compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log) override;
public:
	RandomOrdering(int _random_seed, bool inverted) : ActionOrdering(inverted), random_seed(_random_seed) {}
	std::string get_name() const override {return "random";}
};
}

#endif
