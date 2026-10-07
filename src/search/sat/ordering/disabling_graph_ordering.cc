#include "disabling_graph_ordering.h"

#include "../../algorithms/sccs.h"
#include "../../task_utils/task_properties.h"
#include "../../utils/collections.h"

#include <iomanip>
#include <unordered_set>

using namespace std;

namespace sat_search {

// TODO copied from pruning/stubborn_sets_action_centric.h
// Relies on both fact sets being sorted by variable.
static bool contain_conflicting_fact(const vector<FactPair> &facts1,
                                     const vector<FactPair> &facts2) {
    auto facts1_it = facts1.begin();
    auto facts2_it = facts2.begin();
    while (facts1_it != facts1.end() && facts2_it != facts2.end()) {
        if (facts1_it->var < facts2_it->var) {
            ++facts1_it;
        } else if (facts1_it->var > facts2_it->var) {
            ++facts2_it;
        } else {
            if (facts1_it->value != facts2_it->value)
                return true;
            ++facts1_it;
            ++facts2_it;
        }
    }
    return false;
}

bool DisablingGraphOrdering::can_be_executed_in_same_state(int op1_no, int op2_no) const {
    return !contain_conflicting_fact(sorted_op_preconditions[op1_no],
                                    			   sorted_op_preconditions[op2_no]);
}

bool DisablingGraphOrdering::have_actions_unconflicting_effects(int op1_no, int op2_no) const {
    return !contain_conflicting_fact(sorted_op_effects[op1_no],
                                    			   sorted_op_effects[op2_no]);
}

void DisablingGraphOrdering::set_up_efficient_conflict_testing(const TaskProxy &task_proxy){
	// prepare data structures needed for compatibility checking
	// TODO: copied from pruning/stubborn_sets.cc maybe create common super class
    sorted_op_preconditions = utils::map_vector<vector<FactPair>>(
        task_proxy.get_operators(), [](const OperatorProxy &op) {
            return utils::sorted<FactPair>(
                task_properties::get_fact_pairs(op.get_preconditions()));
        });

    sorted_op_effects = utils::map_vector<vector<FactPair>>(
        task_proxy.get_operators(), [](const OperatorProxy &op) {
			EffectsProxy effProx = op.get_effects();
			vector<EffectProxy> unconditionalEffects;
			for (EffectProxy eff : effProx)
				if (eff.get_conditions().size() == 0)
					unconditionalEffects.push_back(eff);

            return utils::sorted<FactPair>(
                utils::map_vector<FactPair>(
                    unconditionalEffects,
                    [](const EffectProxy &eff) {return eff.get_fact().get_pair();}));
        });
}

ActionOrderingResult DisablingGraphOrdering::compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log){
	const map<FactPair,set<int>> &needingOperators = interference.needing;
	const map<FactPair,set<int>> &deletingOperators = interference.deleting;
	static const set<int> noOperators;
	auto needersOf = [&](const FactPair &fact) -> const set<int> & {
		auto it = needingOperators.find(fact);
		return it == needingOperators.end() ? noOperators : it->second;
	};

	// create data structure that allow for efficient testing whether action have non-conflicting preconditions and effects
	set_up_efficient_conflict_testing(task_proxy);

	ActionOrderingResult result;

	// data structure for the disabling graph. This is only temporary. The actual edges are not needed later on an can be discarded
	// once the global action ordering is determined.
	vector<set<int>> disabling_graph(task_proxy.get_operators().size());
	int number_of_edges_in_disabling_graph = 0;
	int number_refuted_edges_in_disabling_graph = 0;
	unordered_set<int> sequentialOperators;
	set<FactPair> &thresholdFacts = result.unconstrained_facts;

	// actually compute the edges of the graph
	for (const auto &[fact, deleters] : deletingOperators){
		const set<int> &needers = needersOf(fact);
		if (needers.size() == 0) {
			thresholdFacts.insert(fact);
			continue;
		}
		int checkSize = deleters.size() * needers.size();
		//log << "DG " << fact << " deleter " << deleters.size() << " needers " << needers.size() << " checks " << checkSize << endl;
		if (checkSize > disabling_threshold){
			thresholdFacts.insert(fact);
			unordered_set<int> thisSequentialOperators;
			for (int deleter : deleters){
				sequentialOperators.insert(deleter);
				thisSequentialOperators.insert(deleter);
			}
			for (int needer : needers){
				sequentialOperators.insert(needer);
				thisSequentialOperators.insert(needer);
			}

			if (above_threshold_group_joining) continue;

			// TODO: the at-most-one group is currently not encoded
			vector<int> atMostOneGroup;
			for (const int & op : thisSequentialOperators)
				atMostOneGroup.push_back(op);

			continue;
		}
		for (int deleter : deleters){
			for (int needer : needers){
				if (deleter == needer) continue;
				// if preconditions are incompatible, action's don't disable each other
				if (!can_be_executed_in_same_state(deleter,needer)) {
					number_refuted_edges_in_disabling_graph++;
					continue;	
				}
				if (!have_actions_unconflicting_effects(deleter,needer)) {
					number_refuted_edges_in_disabling_graph++;
					continue;	
				}

				// deleter disables needer
				if (disabling_graph[deleter].count(needer)) continue; // only count inserted edges once

				disabling_graph[deleter].insert(needer);
				number_of_edges_in_disabling_graph++;
			}
		}
	}

	log << "Generated Disabling Graph with " << number_of_edges_in_disabling_graph << " edges." << std::endl;
	double refute_quota = double(number_refuted_edges_in_disabling_graph) / (number_of_edges_in_disabling_graph + number_refuted_edges_in_disabling_graph);
	log << "Refuted " << number_refuted_edges_in_disabling_graph << " edges. Quota: " << fixed << setprecision(5) << refute_quota << std::endl;

	log << "Due to threshold, treating " << sequentialOperators.size() << " of " << task_proxy.get_operators().size() << " operators as sequential." << endl; 
	// TODO: the at-most-one group is currently not encoded
	if (above_threshold_group_joining && sequentialOperators.size()){
		vector<int> atMostOneGroup;
		for (const int & op : sequentialOperators)
			atMostOneGroup.push_back(op);
	}

	vector<vector<int>> disabling_graph_vector(task_proxy.get_operators().size());
	for(size_t op = 0; op < task_proxy.get_operators().size(); op++){
		for (const int & op2 : disabling_graph[op]){
			disabling_graph_vector[op].push_back(op2);
		}
	}

	// print to file
	//graph_to_dot(disabling_graph_vector,"disabling.graph");

	result.groups = sccs::compute_maximal_sccs(disabling_graph_vector);
	log << "Disabling Graph contains " << result.groups.size() << " SCCS." << std::endl;
	
	// go backwards though the SCCs
	for (int scc = result.groups.size() - 1; scc >= 0; scc--){
		for (size_t i = 0; i < result.groups[scc].size(); i++){
			const int & opID = result.groups[scc][i];
			result.order.push_back(opID);
		}
	}
	return result;
}
}
