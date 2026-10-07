#include "base_encoding.h"

#include "ipasir.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/markup.h"
#include "../utils/system.h"

#include <iomanip>

using namespace std;

namespace sat_search {

BaseEncodingFactory::BaseEncodingFactory(
		shared_ptr<ActionOrdering> _ordering,
		const string &_write_ordering_file,
		bool _stop_after_ordering,
		bool _encode_fdr_mutexes,
		bool _encode_additional_mutexes,
		bool forceAtLeastOneAction) :
	SATEncodingFactory(forceAtLeastOneAction),
	ordering(_ordering),
	write_ordering_file(_write_ordering_file),
	stop_after_ordering(_stop_after_ordering),
	encode_fdr_mutexes(_encode_fdr_mutexes),
	encode_additional_mutexes(_encode_additional_mutexes) {
}

void BaseEncodingFactory::initialize(const TaskProxy _task_proxy, utils::LogProxy _log) {
	task_proxy = std::make_shared<TaskProxy>(_task_proxy);
	log = std::make_shared<utils::LogProxy>(_log);

	analyse_task();
	compute_action_ordering();
	assert(global_action_ordering.size() == task_proxy->get_operators().size());

	if (!write_ordering_file.empty()){
		write_ordering_to_file(global_action_ordering, write_ordering_file);
		*log << "Action ordering written to " << write_ordering_file << endl;
	}
	if (stop_after_ordering){
		*log << "Stopping after the computation of the action ordering as requested." << endl;
		utils::exit_with(utils::ExitCode::SEARCH_UNSOLVED_INCOMPLETE);
	}

	prepare_encoding();
}

bool BaseEncodingFactory::ordering_requires_interference_info() const {
	return ordering->requires_interference_info();
}

ActionOrderingResult BaseEncodingFactory::compute_ordering(const InterferenceInfo &interference){
	return ordering->compute(*task_proxy, interference, *log);
}

InterferenceInfo BaseEncodingFactory::compute_basic_interference_info() const {
	InterferenceInfo info;
	for (OperatorProxy op : task_proxy->get_operators()){
		map<int,int> preMap;
		for (FactProxy pre : op.get_preconditions()){
			info.needing[pre.get_pair()].insert(op.get_id());
			preMap[pre.get_variable().get_id()] = pre.get_value();
		}
		for (EffectProxy eff : op.get_effects()){
			for (FactProxy cond : eff.get_conditions())
				info.needing[cond.get_pair()].insert(op.get_id());
			FactPair effFact = eff.get_fact().get_pair();
			for (int val = 0; val < eff.get_fact().get_variable().get_domain_size(); val++){
				if (val == effFact.value) continue;
				// if the precondition forces a different value, val cannot be deleted
				if (preMap.count(effFact.var) && preMap[effFact.var] != val) continue;
				info.deleting[FactPair(effFact.var, val)].insert(op.get_id());
			}
		}
	}
	return info;
}


BaseEncoding::BaseEncoding(BaseEncodingFactory* _factory, shared_ptr<sat_capsule> capsule,
		const TaskProxy _task_proxy, bool forceAtLeastOneAction, utils::LogProxy _log) :
	SATEncoding(capsule, _task_proxy, forceAtLeastOneAction),
	base_factory(_factory),
	log(_log) {
}

int BaseEncoding::get_fact_var(int time, FactProxy fact){
	assert(base_factory->statically_true_derived_predicates.count(fact.get_variable().get_id()) == 0);
	return fact_variables[time][fact.get_variable().get_id()][fact.get_value()];
}

int BaseEncoding::get_fact_var(int time, FactPair fact){
	return fact_variables[time][fact.var][fact.value];
}

void BaseEncoding::generate_operator_variables(int time){
	for (size_t op = 0; op < task_proxy.get_operators().size(); op ++){
		int opvar = sat->new_variable();
		operator_variables[time].push_back(opvar);
		variableCounter["operator"]++;
		DEBUG(sat->registerVariable(opvar,"op " + utils::pad_int(op) + " @ " + utils::pad_int(time) + " " + task_proxy.get_operators()[op].get_name()));
	}
}

void BaseEncoding::generate_fact_variables(int time){
	fact_variables[time].resize(task_proxy.get_variables().size());
	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
		// don't need to generate anything for statically true facts.
		if (base_factory->statically_true_derived_predicates.count(var)) continue;
		VariableProxy varProxy = task_proxy.get_variables()[var];
		// we use a non-compressed encoding -- one hot
		// TODO strategy pattern for binary encoding -- as for each variable only one value can be true at any time!
		for (int val = 0; val < varProxy.get_domain_size(); val++){
			int factVar = sat->new_variable();
			fact_variables[time][var].push_back(factVar);
			variableCounter["facts"]++;
			DEBUG(sat->registerVariable(factVar,"fa " + utils::pad_int(var) + "=" + utils::pad_int(val) + " @ " + utils::pad_int(time) + " " + varProxy.get_name() + "=" + varProxy.get_fact(val).get_name()));
		}
	}

	// encode mutex clauses for the state -- we do it at generation time as we want to ensure they are encoded only once
	mutex_encoding_for_timestep(time);

	registerClauses("state mutexes");
}

void BaseEncoding::mutex_encoding_for_timestep(int time){
	// State Mutexes based on FDR variables
	if (base_factory->encode_fdr_mutexes){
		for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
			if (base_factory->statically_true_derived_predicates.count(var)) continue;
			sat->atMostOne(fact_variables[time][var]);
			sat->atLeastOne(fact_variables[time][var]);
		}
	}

	// additional mutexes from the input
	if (base_factory->encode_additional_mutexes){
		MutexesProxy mutexes = task_proxy.get_mutexes();
		for (size_t m = 0; m < mutexes.size(); m++){
			MutexProxy mutex = mutexes[m];
			FactProxy f1 = mutex.get_first();
			FactProxy f2 = mutex.get_second();

			sat->impliesNot(get_fact_var(time,f1), get_fact_var(time, f2));
		}
	}
}

int BaseEncoding::get_derived_goal_var(int, FactProxy){
	log << "Error: this encoding does not support derived predicates." << endl;
	utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
}

//////////////////////////////////////////////////////////////////
// Initial state
void BaseEncoding::encodeInit(int fromTime, bool) {
	State init = task_proxy.get_initial_state();
	init.unpack();
	for (size_t i = 0; i < init.size(); i++){
		//if (init[i].get_variable().is_derived()) continue;
		if (base_factory->statically_true_derived_predicates.count(init[i].get_variable().get_id())) continue;
		sat->assertYes(get_fact_var(fromTime,init[i]));
	}
	registerClauses("init");
}

//////////////////////////////////////////////////////////////////
// Goal state
void BaseEncoding::encodeGoal(int toTime, bool) {
	GoalsProxy goals = task_proxy.get_goals();
	for (size_t i = 0; i < goals.size(); i++){
		if (base_factory->statically_true_derived_predicates.count(goals[i].get_variable().get_id()) == 1){
			assert(goals[i].get_value() == 1);
			// statically true goals do not have to be satisfied.
			continue;
		}

		if (goals[i].get_variable().is_derived()){
			DEBUG(log << "Derived GOAL " << goals[i].get_variable().get_id() << " " << goals[i].get_value() << " " << get_derived_goal_var(toTime,goals[i]) << endl);
			sat->assertYes(get_derived_goal_var(toTime,goals[i]));
		} else {
			DEBUG(log << "Regular GOAL " << goals[i].get_variable().get_id() << " " << goals[i].get_value() << " " << get_fact_var(toTime,goals[i]) << endl);
			sat->assertYes(get_fact_var(toTime,goals[i]));
		}
	}
	registerClauses("goal");

	//////////////////////////////////////////////////////////////////
	// Generation of formula done
	//DEBUG(sat->printVariables());
	print_formula_statistics();
}

void BaseEncoding::print_formula_statistics(){
	// print variable and clause statistics
	log << "Variables" << setw(23) << setfill(' ') << "total: " << setw(8) << setfill(' ') << sat->number_of_variables << endl;
	for (auto [a,b] : variableCounter)
		log << setw(30) << setfill(' ') << a << ": " << setw(8) << setfill(' ') << b << endl;
	log << "Clauses" << setw(25) << setfill(' ') << "total: " << setw(8) << setfill(' ') << sat->get_number_of_clauses() << endl;
	for (auto [a,b] : clauseCounter)
		log << setw(30) << setfill(' ') << a << ": " << setw(8) << setfill(' ') << b << endl;
}

void BaseEncoding::encodeStateEquals(int, int, bool){
}

std::tuple<State,std::vector<State>,Plan> BaseEncoding::extractSolution(int, std::vector<std::pair<int,int>> time_step_order) {
	// maps operator to their index in the global ordering
	std::vector<int> global_action_indexing(task_proxy.get_operators().size());
	for(size_t i = 0; i < base_factory->global_action_ordering.size(); i++)
		global_action_indexing[base_factory->global_action_ordering[i]] = i;

	Plan plan;

	// plan extraction
	for (auto [time,toTime] : time_step_order){
		map<int,int> operatorsThisTime;
		for (size_t op = 0; op < task_proxy.get_operators().size(); op++){
			int opvar = operator_variables[time][op];
			int val = ipasir_val(sat->solver,opvar);
			if (val > 0)
				operatorsThisTime[global_action_indexing[op]] = op;
		}

		// sort the operators according to their global sorting
		for (auto & [_sortkey, op] : operatorsThisTime){
			plan.push_back(OperatorID(op));
			log << "time " << time << " Event: " << plan.size() << " sorted operator " << task_proxy.get_operators()[op].get_name() << endl;
		}
	}

	// execute the plan to obtain the states and to check that it is valid
	OperatorsProxy operators = task_proxy.get_operators();
	State cur = task_proxy.get_initial_state();
	cur.unpack();
	vector<State> visited_states;
	visited_states.push_back(cur);

	for (size_t i = 0; i < plan.size(); ++i) {
		if (!task_properties::is_applicable(operators[plan[i]], cur)){
			log << "Error: extracted plan is invalid. Operator " << i << " (" << operators[plan[i]].get_name()
				<< ") is not applicable." << endl;
			utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
		}
		// successor generation evaluates axioms
	    cur = cur.get_unregistered_successor(operators[plan[i]]);
		cur.unpack();
	    visited_states.push_back(cur);
	}
	if (!task_properties::is_goal_state(task_proxy, cur)){
		log << "Error: extracted plan is invalid. It does not reach the goal." << endl;
		utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
	}

	return make_tuple(visited_states.back(),visited_states,plan);
}

// functions for debugging
void BaseEncoding::assertLabelsAtTime(int, std::set<int>) {
}


void add_base_encoding_options_to_feature(plugins::Feature &feature, const string &default_ordering){
	feature.add_option<shared_ptr<ActionOrdering>>(
		"ordering",
		"action ordering, i.e. order in which the actions of a time step are executed",
		default_ordering);
	feature.add_option<string>(
		"write_ordering",
		"if not empty, write the action ordering (operator IDs in execution order) to this file",
		"\"\"");
	feature.add_option<bool>(
		"stop_after_ordering",
		"stop the planner after the action ordering has been computed (and written)",
		"false");
	feature.add_option<bool>(
		"fdr_mutexes",
		"encode mutexes based on fdr variables",
		"true");
	feature.add_option<bool>(
		"additional_mutexes",
		"encode any additional mutex that is found",
		"true");
	feature.add_option<bool>(
		"forceAtLeastOneAction",
		"force at least one action per timestep",
		"true");
}

tuple<shared_ptr<ActionOrdering>, string, bool, bool, bool, bool>
get_base_encoding_arguments_from_options(const plugins::Options &opts){
	return make_tuple(
		opts.get<shared_ptr<ActionOrdering>>("ordering"),
		opts.get<string>("write_ordering"),
		opts.get<bool>("stop_after_ordering"),
		opts.get<bool>("fdr_mutexes"),
		opts.get<bool>("additional_mutexes"),
		opts.get<bool>("forceAtLeastOneAction"));
}
}
