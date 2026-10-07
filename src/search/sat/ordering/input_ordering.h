#ifndef SAT_ORDERING_INPUT_ORDERING_H
#define SAT_ORDERING_INPUT_ORDERING_H

#include "action_ordering.h"

namespace sat_search {

// Operators are ordered as they appear in the input (SAS) file.
class InputOrdering : public ActionOrdering {
protected:
	ActionOrderingResult compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log) override;
public:
	explicit InputOrdering(bool inverted) : ActionOrdering(inverted) {}
	std::string get_name() const override {return "input";}
};
}

#endif
