#include "exists_step_sigma.h"

#include "../utils/markup.h"
#include "../utils/system.h"

#include <map>

using namespace std;

namespace sat_search {

void ExistsStepSigmaEncodingFactory::analyse_task(){
	if (task_proxy->get_axioms().size() > 0){
		*log << "Error: the sigma encoding does not support axioms." << endl;
		utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
	}
	for (OperatorProxy op : task_proxy->get_operators())
		for (EffectProxy eff : op.get_effects())
			if (eff.get_conditions().size() > 0){
				*log << "Error: the sigma encoding does not support conditional effects." << endl;
				utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
			}
}

void ExistsStepSigmaEncodingFactory::compute_action_ordering(){
	InterferenceInfo interference;
	if (ordering_requires_interference_info())
		interference = compute_basic_interference_info();
	global_action_ordering = compute_ordering(interference).order;
}

void ExistsStepSigmaEncodingFactory::prepare_encoding(){
	OperatorsProxy operators = task_proxy->get_operators();
	VariablesProxy variables = task_proxy->get_variables();

	// Pruning relies on the invariant that at most one value per variable is true in every intermediate state.
	// It holds as the state mutexes (AMO) are always encoded and the sigma definitions propagate it.
	bool prune = prune_deletes;

	preconditions.assign(operators.size(), {});
	changes.assign(operators.size(), {});

	size_t numChanges = 0;
	for (int op : global_action_ordering){
		OperatorProxy opProxy = operators[op];
		map<int,int> preMap;
		for (FactProxy pre : opProxy.get_preconditions()){
			preconditions[op].push_back(pre.get_pair());
			preMap[pre.get_pair().var] = pre.get_pair().value;
		}
		for (EffectProxy eff : opProxy.get_effects()){
			FactPair fact = eff.get_fact().get_pair();
			auto pre = preMap.find(fact.var);
			// precondition var=v and effect var=v: the state does not change
			if (pre != preMap.end() && pre->second == fact.value) continue;
			changes[op].push_back({fact, true, false});
			for (int val = 0; val < variables[fact.var].get_domain_size(); val++){
				if (val == fact.value) continue;
				// With precondition var=u, all values other than u are already false when the operator is applied.
				if (prune && pre != preMap.end() && pre->second != val) continue;
				changes[op].push_back({FactPair(fact.var, val), false, false});
			}
		}
		numChanges += changes[op].size();
	}

	// mark the last change of each fact
	vector<vector<bool>> changed(variables.size());
	for (VariableProxy var : variables)
		changed[var.get_id()].assign(var.get_domain_size(), false);
	for (auto it = global_action_ordering.rbegin(); it != global_action_ordering.rend(); ++it)
		for (Change &change : changes[*it])
			if (!changed[change.fact.var][change.fact.value]){
				changed[change.fact.var][change.fact.value] = true;
				change.is_last = true;
			}
	unchanged_facts.clear();
	for (VariableProxy var : variables)
		for (int val = 0; val < var.get_domain_size(); val++)
			if (!changed[var.get_id()][val])
				unchanged_facts.emplace_back(var.get_id(), val);

	*log << "Sigma: " << numChanges << " fact changes per step, " << unchanged_facts.size() << " facts never change." << endl;
}

unique_ptr<SATEncoding> ExistsStepSigmaEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule){
	return make_unique<ExistsStepSigmaEncoding>(this, capsule, *task_proxy, forceAtLeastOneAction, *log);
}


ExistsStepSigmaEncoding::ExistsStepSigmaEncoding(
		ExistsStepSigmaEncodingFactory* _factory,
		shared_ptr<sat_capsule> capsule, const TaskProxy _task_proxy, bool forceAtLeastOneAction,
		utils::LogProxy _log) :
	BaseEncoding(_factory, capsule, _task_proxy, forceAtLeastOneAction, _log),
	factory(_factory) {
}

void ExistsStepSigmaEncoding::encode(int fromTime, int toTime){
	curClauseNumber = sat->get_number_of_clauses();

	if (operator_variables.count(fromTime) == 0) generate_operator_variables(fromTime);
	if (fact_variables.count(fromTime) == 0) generate_fact_variables(fromTime);
	if (fact_variables.count(toTime) == 0) generate_fact_variables(toTime);
	registerClauses("state mutexes");

	const vector<int> &opVars = operator_variables[fromTime];
	const vector<vector<int>> &endState = fact_variables[toTime];
	// sigma: the SAT variable representing each fact in the current intermediate state
	vector<vector<int>> sigma = fact_variables[fromTime];

	for (int op : factory->global_action_ordering){
		int a = opVars[op];
		// preconditions are evaluated in the state before the operator
		for (const FactPair &pre : factory->preconditions[op])
			sat->implies(a, sigma[pre.var][pre.value]);
		registerClauses("preconditions");

		for (const auto &change : factory->changes[op]){
			int prev = sigma[change.fact.var][change.fact.value];
			int next;
			if (change.is_last)
				next = endState[change.fact.var][change.fact.value];
			else {
				next = sat->new_variable();
				variableCounter["sigma"]++;
				DEBUG(sat->registerVariable(next, "sigma " + utils::pad_int(change.fact.var) + "=" + utils::pad_int(change.fact.value) + " op " + utils::pad_int(op) + " @ " + utils::pad_int(fromTime)));
			}
			if (change.becomes_true){
				// next <-> prev v a
				sat->impliesOr(next, {prev, a});
				sat->implies(prev, next);
				sat->implies(a, next);
			} else {
				// next <-> prev ^ -a
				sat->implies(next, prev);
				sat->impliesNot(next, a);
				sat->impliesOr(prev, {next, a});
			}
			sigma[change.fact.var][change.fact.value] = next;
		}
		registerClauses("sigma");
	}

	// facts that no operator changes keep their value
	for (const FactPair &fact : factory->unchanged_facts){
		int before = fact_variables[fromTime][fact.var][fact.value];
		int after = endState[fact.var][fact.value];
		sat->implies(before, after);
		sat->implies(after, before);
	}
	registerClauses("frame");

	if (forceAtLeastOneAction) sat->atLeastOne(opVars);
	registerClauses("action control");
}
}
