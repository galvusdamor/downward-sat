#include "topological_ordering.h"

#include <tuple>

using namespace std;

namespace sat_search {

ActionOrderingResult TopologicalOrdering::compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &, utils::LogProxy &){
	OperatorsProxy operators = task_proxy.get_operators();
	int number_of_operators = operators.size();

	// supporters: var -> value -> operators having that fact as an effect
	vector<vector<vector<int>>> supporters(task_proxy.get_variables().size());
	for (VariableProxy var : task_proxy.get_variables())
		supporters[var.get_id()].resize(var.get_domain_size());
	// preconditions of each operator
	vector<vector<FactPair>> preconditions(number_of_operators);
	for (OperatorProxy op : operators){
		for (EffectProxy eff : op.get_effects()){
			FactPair f = eff.get_fact().get_pair();
			supporters[f.var][f.value].push_back(op.get_id());
		}
		for (FactProxy pre : op.get_preconditions())
			preconditions[op.get_id()].push_back(pre.get_pair());
	}

	// iterative version of the recursive DFS in Figure 3.3 (recursion depth could be the number of operators)
	vector<int> order;
	vector<bool> visited(number_of_operators, false);
	// stack frame: operator, index of current precondition, index of current supporter of that precondition
	vector<tuple<int,size_t,size_t>> stack;
	for (int start = 0; start < number_of_operators; start++){
		if (visited[start]) continue;
		visited[start] = true;
		stack.emplace_back(start, 0, 0);
		while (!stack.empty()){
			auto &[op, pre, sup] = stack.back();
			int next = -1;
			while (pre < preconditions[op].size()){
				const vector<int> &sups = supporters[preconditions[op][pre].var][preconditions[op][pre].value];
				if (sup < sups.size()){
					int candidate = sups[sup++];
					if (!visited[candidate]){
						next = candidate;
						break;
					}
				} else {
					pre++;
					sup = 0;
				}
			}
			if (next == -1){
				// all supporters ranked
				order.push_back(op);
				stack.pop_back();
			} else {
				visited[next] = true;
				stack.emplace_back(next, 0, 0);
			}
		}
	}
	return make_single_group_result(move(order));
}
}
