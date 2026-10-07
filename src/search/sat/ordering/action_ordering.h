#ifndef SAT_ORDERING_ACTION_ORDERING_H
#define SAT_ORDERING_ACTION_ORDERING_H

#include "../../task_proxy.h"
#include "../../utils/logging.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace plugins {
class Feature;
class Options;
}

namespace sat_search {

// Interference information about the operators of a task, computed by the encoding.
// needing[f]: operators that need fact f (preconditions, effect conditions, ...)
// deleting[f]: operators that may make fact f false
struct InterferenceInfo {
	std::map<FactPair,std::set<int>> needing;
	std::map<FactPair,std::set<int>> deleting;
};

struct ActionOrderingResult {
	// operator IDs in execution order (the first operator has the lowest rank)
	std::vector<int> order;
	// Partition of the operators into groups. Interference between operators has to be
	// handled only inside a group, the order between groups is already safe.
	// Inside each group, the operators are listed in execution order.
	// Orderings that don't compute such information return a single group containing all operators.
	std::vector<std::vector<int>> groups;
	// facts for which no interference restrictions are generated (e.g. due to a size threshold)
	std::set<FactPair> unconstrained_facts;
};

class ActionOrdering {
	const bool inverted;
protected:
	virtual ActionOrderingResult compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log) = 0;
public:
	explicit ActionOrdering(bool _inverted) : inverted(_inverted) {}
	virtual ~ActionOrdering() = default;

	// whether compute needs the InterferenceInfo. If false, an empty one may be passed.
	virtual bool requires_interference_info() const {return false;}
	virtual std::string get_name() const = 0;

	ActionOrderingResult compute(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log);
};

// result with a single group, i.e. the full order
ActionOrderingResult make_single_group_result(std::vector<int> order);

void write_ordering_to_file(const std::vector<int> &order, const std::string &filename);
// reads whitespace separated operator IDs; exits with an input error if not a permutation of 0..number_of_operators-1
std::vector<int> read_ordering_from_file(const std::string &filename, int number_of_operators);

extern void add_action_ordering_options_to_feature(plugins::Feature &feature);
extern std::tuple<bool> get_action_ordering_arguments_from_options(const plugins::Options &opts);
}

#endif
