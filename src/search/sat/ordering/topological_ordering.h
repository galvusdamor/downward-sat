#ifndef SAT_ORDERING_TOPOLOGICAL_ORDERING_H
#define SAT_ORDERING_TOPOLOGICAL_ORDERING_H

#include "action_ordering.h"

namespace sat_search {

// Topological Ranking (TSort) by Balyo (thesis, Section 3.4.2, Figure 3.3).
// Topologically sorts the enabling graph (a -> a' iff eff(a) and pre(a') intersect) ignoring cycles:
// before an operator is ranked, all operators supporting one of its preconditions are ranked (DFS post-order).
class TopologicalOrdering : public ActionOrdering {
protected:
	ActionOrderingResult compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log) override;
public:
	explicit TopologicalOrdering(bool inverted) : ActionOrdering(inverted) {}
	std::string get_name() const override {return "tsort";}
};
}

#endif
