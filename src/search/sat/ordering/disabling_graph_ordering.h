#ifndef SAT_ORDERING_DISABLING_GRAPH_ORDERING_H
#define SAT_ORDERING_DISABLING_GRAPH_ORDERING_H

#include "action_ordering.h"

namespace sat_search {

// Ordering of Rintanen et al. for the exists-step encoding based on the SCCs of the disabling graph.
// Edge a -> b if a may delete a fact needed by b (and a and b could be executed together).
// The groups of the result are the SCCs of the disabling graph.
class DisablingGraphOrdering : public ActionOrdering {
	const int disabling_threshold;
	const bool above_threshold_group_joining;

	// data structures for testing whether two actions have compatible preconditions and effects
	std::vector<std::vector<FactPair>> sorted_op_preconditions;
	std::vector<std::vector<FactPair>> sorted_op_effects;
	void set_up_efficient_conflict_testing(const TaskProxy &task_proxy);
	bool can_be_executed_in_same_state(int op1_no, int op2_no) const;
	bool have_actions_unconflicting_effects(int op1_no, int op2_no) const;
protected:
	ActionOrderingResult compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log) override;
public:
	DisablingGraphOrdering(int _disabling_threshold, bool _above_threshold_group_joining, bool inverted) :
		ActionOrdering(inverted), disabling_threshold(_disabling_threshold),
		above_threshold_group_joining(_above_threshold_group_joining) {}
	bool requires_interference_info() const override {return true;}
	std::string get_name() const override {return "disabling";}
};
}

#endif
