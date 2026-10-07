#include "relaxed_relaxed_exists_step.h"

#include "../utils/markup.h"
#include "../utils/system.h"

#include <algorithm>

using namespace std;

namespace sat_search {

void RelaxedRelaxedExistsStepEncodingFactory::analyse_task(){
	if (task_proxy->get_axioms().size() > 0){
		*log << "Error: the R2E encoding does not support axioms." << endl;
		utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
	}
	for (OperatorProxy op : task_proxy->get_operators())
		for (EffectProxy eff : op.get_effects())
			if (eff.get_conditions().size() > 0){
				*log << "Error: the R2E encoding does not support conditional effects." << endl;
				utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
			}
}

void RelaxedRelaxedExistsStepEncodingFactory::compute_action_ordering(){
	InterferenceInfo interference;
	if (ordering_requires_interference_info())
		interference = compute_basic_interference_info();
	global_action_ordering = compute_ordering(interference).order;
}

void RelaxedRelaxedExistsStepEncodingFactory::prepare_encoding(){
	OperatorsProxy operators = task_proxy->get_operators();
	VariablesProxy variables = task_proxy->get_variables();

	rank.assign(operators.size(), -1);
	for (size_t i = 0; i < global_action_ordering.size(); i++)
		rank[global_action_ordering[i]] = i;

	// Static information on operators. We iterate over the operators in rank order such that all lists are sorted by rank.
	supporters.assign(variables.size(), {});
	for (VariableProxy var : variables)
		supporters[var.get_id()].resize(var.get_domain_size());
	scope_operators.assign(variables.size(), {});
	preconditions.assign(operators.size(), {});
	effprev.assign(operators.size(), {});

	// fact -> operators requiring / opposing it (sorted by rank)
	vector<vector<vector<int>>> requirers(variables.size());
	vector<vector<vector<int>>> opponents(variables.size());
	for (VariableProxy var : variables){
		requirers[var.get_id()].resize(var.get_domain_size());
		opponents[var.get_id()].resize(var.get_domain_size());
	}

	for (int op : global_action_ordering){
		OperatorProxy opProxy = operators[op];
		map<int,int> preMap;
		for (FactProxy pre : opProxy.get_preconditions()){
			FactPair fact = pre.get_pair();
			preconditions[op].push_back(fact);
			preMap[fact.var] = fact.value;
			requirers[fact.var][fact.value].push_back(op);
		}
		set<int> effectVars;
		for (EffectProxy eff : opProxy.get_effects()){
			FactPair fact = eff.get_fact().get_pair();
			effectVars.insert(fact.var);
			effprev[op].push_back(fact);
			scope_operators[fact.var].push_back(op);
			supporters[fact.var][fact.value].push_back(op);
			for (int val = 0; val < variables[fact.var].get_domain_size(); val++){
				if (val == fact.value) continue;
				// If the precondition requires var=u, then all other values of var are already false when the operator
				// is executed. So the operator can only destroy var=u.
				if (prune_opponents && preMap.count(fact.var) && preMap[fact.var] != val) continue;
				opponents[fact.var][val].push_back(op);
			}
		}
		// prevail conditions
		for (auto [var, val] : preMap){
			if (effectVars.count(var)) continue;
			effprev[op].push_back(FactPair(var, val));
			scope_operators[var].push_back(op);
		}
	}

	// interference chains
	size_t chainElements = 0;
	for (VariableProxy varProxy : variables){
		int var = varProxy.get_id();
		for (int val = 0; val < varProxy.get_domain_size(); val++){
			const vector<int> &R = requirers[var][val];
			const vector<int> &O = opponents[var][val];
			const vector<int> &S = supporters[var][val];
			if (R.empty() || O.empty()) continue;
			// chain is only needed from the first opponent up to the last requirer
			int firstOpponentRank = rank[O.front()];
			int lastRequirerRank = rank[R.back()];
			if (firstOpponentRank >= lastRequirerRank) continue;

			// merge the three lists
			map<int,ChainElement> elements; // rank -> element
			auto get_element = [&](int op) -> ChainElement & {
				auto [it, inserted] = elements.try_emplace(rank[op], ChainElement{op, false, false, false});
				return it->second;
			};
			for (int op : R)
				if (rank[op] > firstOpponentRank && rank[op] <= lastRequirerRank)
					get_element(op).is_requirer = true;
			for (int op : O)
				if (rank[op] < lastRequirerRank)
					get_element(op).is_opponent = true;
			for (int op : S)
				if (rank[op] > firstOpponentRank && rank[op] < lastRequirerRank)
					get_element(op).is_supporter = true;

			vector<ChainElement> chain;
			for (auto & [_rank, element] : elements)
				chain.push_back(element);
			chainElements += chain.size();
			chains.emplace_back(FactPair(var, val), move(chain));
		}
	}
	*log << "R2E: generated " << chains.size() << " interference chains with " << chainElements << " elements." << endl;
}

unique_ptr<SATEncoding> RelaxedRelaxedExistsStepEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule){
	return make_unique<RelaxedRelaxedExistsStepEncoding>(this, capsule, *task_proxy, forceAtLeastOneAction, *log);
}


RelaxedRelaxedExistsStepEncoding::RelaxedRelaxedExistsStepEncoding(
		RelaxedRelaxedExistsStepEncodingFactory* _factory,
		shared_ptr<sat_capsule> capsule, const TaskProxy _task_proxy, bool forceAtLeastOneAction,
		utils::LogProxy _log) :
	BaseEncoding(_factory, capsule, _task_proxy, forceAtLeastOneAction, _log),
	factory(_factory) {
}

// Equation 3.21: a precondition is either true at the beginning of the step or achieved by a lower ranked operator.
void RelaxedRelaxedExistsStepEncoding::encode_preconditions(int time){
	const vector<int> &opVars = operator_variables[time];
	for (size_t op = 0; op < opVars.size(); op++){
		int opRank = factory->rank[op];
		for (const FactPair &pre : factory->preconditions[op]){
			vector<int> causes;
			causes.push_back(get_fact_var(time, pre));
			for (int supporter : factory->supporters[pre.var][pre.value]){
				if (factory->rank[supporter] >= opRank) break; // sorted by rank
				causes.push_back(opVars[supporter]);
			}
			sat->impliesOr(opVars[op], causes);
		}
	}
	registerClauses("preconditions");
}

// Equation 3.22: an effect or prevail condition holds at the end of the step, unless a higher ranked operator
// with the same variable in its scope is executed.
void RelaxedRelaxedExistsStepEncoding::encode_effects(int fromTime, int toTime){
	const vector<int> &opVars = operator_variables[fromTime];
	const vector<int> &rank = factory->rank;
	for (size_t op = 0; op < opVars.size(); op++){
		int opRank = rank[op];
		for (const FactPair &fact : factory->effprev[op]){
			vector<int> causes;
			causes.push_back(get_fact_var(toTime, fact));
			const vector<int> &scope = factory->scope_operators[fact.var];
			auto it = upper_bound(scope.begin(), scope.end(), opRank,
				[&rank](int r, int other){return r < rank[other];});
			for (; it != scope.end(); ++it)
				causes.push_back(opVars[*it]);
			sat->impliesOr(opVars[op], causes);
		}
	}
	registerClauses("effects");
}

// Equation 3.5: a fact can only become true if an operator achieves it.
void RelaxedRelaxedExistsStepEncoding::encode_frame_axioms(int fromTime, int toTime){
	const vector<int> &opVars = operator_variables[fromTime];
	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
		for (size_t val = 0; val < factory->supporters[var].size(); val++){
			vector<int> achievers;
			for (int op : factory->supporters[var][val])
				achievers.push_back(opVars[op]);
			sat->impliesPosAndNegImpliesOr(fact_variables[toTime][var][val], fact_variables[fromTime][var][val], achievers);
		}
	}
	registerClauses("frame axioms");
}

// Equation 3.23: if an opponent destroys a fact, no higher ranked operator requiring it may be executed,
// unless a supporter in between restores it. The chain variable h is true if the fact may be destroyed.
// Chain variables are shared between consecutive positions where this does not change the semantics.
void RelaxedRelaxedExistsStepEncoding::encode_interference_chains(int time){
	const vector<int> &opVars = operator_variables[time];
	for (const auto & [fact, chain] : factory->chains){
		int h = 0; // current chain variable, 0 if none yet
		bool usedByRequirer = false;
		for (const auto &element : chain){
			int opVar = opVars[element.op];
			// the precondition is checked before the operator's own effect
			if (element.is_requirer && h){
				sat->impliesNot(h, opVar);
				usedByRequirer = true;
			}
			if (element.is_opponent){
				if (h == 0 || usedByRequirer){
					int newH = sat->new_variable();
					variableCounter["chain"]++;
					DEBUG(sat->registerVariable(newH, "chain " + utils::pad_int(fact.var) + "=" + utils::pad_int(fact.value) + " op " + utils::pad_int(element.op) + " @ " + utils::pad_int(time)));
					if (h) sat->implies(h, newH);
					h = newH;
					usedByRequirer = false;
				}
				sat->implies(opVar, h);
			} else if (element.is_supporter && h){
				int newH = sat->new_variable();
				variableCounter["chain"]++;
				DEBUG(sat->registerVariable(newH, "chain " + utils::pad_int(fact.var) + "=" + utils::pad_int(fact.value) + " op " + utils::pad_int(element.op) + " @ " + utils::pad_int(time)));
				sat->impliesOr(h, {newH, opVar});
				h = newH;
				usedByRequirer = false;
			}
		}
	}
	registerClauses("interference chains");
}

void RelaxedRelaxedExistsStepEncoding::encode(int fromTime, int toTime){
	curClauseNumber = sat->get_number_of_clauses();

	if (operator_variables.count(fromTime) == 0) generate_operator_variables(fromTime);
	if (fact_variables.count(fromTime) == 0) generate_fact_variables(fromTime);
	if (fact_variables.count(toTime) == 0) generate_fact_variables(toTime);

	encode_preconditions(fromTime);
	encode_effects(fromTime, toTime);
	encode_frame_axioms(fromTime, toTime);
	encode_interference_chains(fromTime);

	if (forceAtLeastOneAction) sat->atLeastOne(operator_variables[fromTime]);
	registerClauses("action control");
}
}
