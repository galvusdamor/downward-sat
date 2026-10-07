#ifndef SAT_BASE_ENCODING_H
#define SAT_BASE_ENCODING_H

#include "sat_encoder.h"
#include "sat_encoding.h"
#include "ordering/action_ordering.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#define registerClauses(NAME) clauseCounter[NAME] += sat->get_number_of_clauses() - curClauseNumber; curClauseNumber = sat->get_number_of_clauses();

namespace plugins {
class Feature;
class Options;
}

namespace sat_search {

// Common base of the factories of the encodings that use one SAT variable per operator and time step and
// a one-hot encoding of the state at every time step. The factory determines the action ordering (the order in
// which the operators of a step are executed) and can write it to a file.
class BaseEncodingFactory : public SATEncodingFactory {
	std::shared_ptr<ActionOrdering> ordering;
	const std::string write_ordering_file;
	const bool stop_after_ordering;
public:
	const bool encode_fdr_mutexes;
	const bool encode_additional_mutexes;

	// static knowledge about the problem:
	// whether a derived predicate is statically true. Only filled by encodings that support axioms.
	std::unordered_set<int> statically_true_derived_predicates;

	// operator IDs in the order in which they are executed inside a time step
	std::vector<int> global_action_ordering;

protected:
	// called first in initialize, e.g. to reject unsupported tasks or analyse the axioms
	virtual void analyse_task() = 0;
	// has to set global_action_ordering. Typically uses compute_ordering.
	virtual void compute_action_ordering() = 0;
	// called after the ordering has been determined (and potentially been written to a file)
	virtual void prepare_encoding() {}

	bool ordering_requires_interference_info() const;
	ActionOrderingResult compute_ordering(const InterferenceInfo &interference);
	// interference based on preconditions and (all) effects, also of conditional effects. Ignores axioms.
	InterferenceInfo compute_basic_interference_info() const;

public:
	BaseEncodingFactory(
			std::shared_ptr<ActionOrdering> _ordering,
			const std::string &_write_ordering_file,
			bool _stop_after_ordering,
			bool _encode_fdr_mutexes,
			bool _encode_additional_mutexes,
			bool forceAtLeastOneAction);
	virtual ~BaseEncodingFactory() = default;

	void initialize(const TaskProxy _task_proxy, utils::LogProxy _log) override;
};


class BaseEncoding : public SATEncoding {
	BaseEncodingFactory* base_factory;
public:
	mutable utils::LogProxy log;

	// dynamic variables generated while creating the formula
	// index: timestep -> operator
	std::map<int,std::vector<int>> operator_variables;

	// index: timestep -> variable -> value
	std::map<int,std::vector<std::vector<int>>> fact_variables;
	int get_fact_var(int time, FactProxy fact);
	int get_fact_var(int time, FactPair fact);

	int curClauseNumber;
	std::map<std::string,int> clauseCounter;
	std::map<std::string,int> variableCounter;

	void generate_operator_variables(int time);
	// also encodes the state mutexes of the time step
	void generate_fact_variables(int time);
	void mutex_encoding_for_timestep(int time);

	// SAT variable representing that a derived goal is true. Only needed for encodings that support axioms.
	virtual int get_derived_goal_var(int time, FactProxy goal);

	void print_formula_statistics();

public:
	BaseEncoding(BaseEncodingFactory* _factory, std::shared_ptr<sat_capsule> capsule,
		const TaskProxy _task_proxy, bool forceAtLeastOneAction, utils::LogProxy _log);

	void encodeInit(int fromTime, bool retractable) override;
	void encodeGoal(int toTime, bool retractable) override;
	void encodeStateEquals(int fromTime, int toTime, bool retractable) override;
	// orders the operators of each time step according to the global action ordering and checks the plan
	std::tuple<State,std::vector<State>,Plan> extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;

	// functions for debugging
	void assertLabelsAtTime(int fromTime, std::set<int> labels) override;
};


extern void add_base_encoding_options_to_feature(plugins::Feature &feature, const std::string &default_ordering);
extern std::tuple<std::shared_ptr<ActionOrdering>, std::string, bool, bool, bool, bool>
get_base_encoding_arguments_from_options(const plugins::Options &opts);
}

#endif
