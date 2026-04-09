#include <cmath>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "kautz_selman_rintanen.h"
#include "kissat-p.h"

#include "../algorithms/sccs.h"

#include "../utils/markup.h"
#include "../axioms.h"


//#include "../tasks/root_task.h"
#include "../task_utils/task_properties.h"

#include "sat_encoder.h"
#include "ipasir.h"

using namespace std;

#define registerClauses(NAME) clauseCounter[NAME] += sat->get_number_of_clauses() - curClauseNumber; curClauseNumber = sat->get_number_of_clauses();


namespace sat_search {

	
void SaseEncodingFactory::initialize(const TaskProxy _task_proxy, utils::LogProxy _log) {

	task_proxy = std::make_shared<TaskProxy>(_task_proxy);
	log = std::make_shared<utils::LogProxy>(_log);
	// set up data structures for derived predicates and axioms -- won't do anything if none of them exist.
	set_up_axioms();

	// prepare for parallelism encodings 
	if (encoding == 2)
		set_up_exists_step();
	else
		set_up_single_step();

	assert(global_action_ordering.size() == task_proxy->get_operators().size());
}
	
	
std::unique_ptr<SATEncoding> SaseEncodingFactory::createEncodingInstance(std::shared_ptr<sat_capsule> capsule){
	return make_unique<SaseEncoding>(this,capsule,*task_proxy,forceAtLeastOneAction,*log);

};

	
	
SaseEncoding::SaseEncoding(
	SaseEncodingFactory* _factory,
	std::shared_ptr<sat_capsule> capsule,
	const TaskProxy _task_proxy,
	bool forceAtLeastOneAction,
	utils::LogProxy _log)
	:SATEncoding(capsule,_task_proxy,forceAtLeastOneAction),
	factory(_factory),
	log(_log)
	{

}


void SaseEncoding::printVariableTruth(){
	for (int v = 1; v <= sat->number_of_variables; v++){
		int val = ipasir_val(sat->solver,v);
	
		std::string s = std::to_string(v);
		int x = 4 - s.size();
		while (x-- && x > 0) std::cout << " ";
		std::cout << v << ": ";
		if (val > 0) std::cout << "    ";
		else         std::cout << "not ";
#ifndef NDEBUG
		std::cout << sat->variableNames[v] << endl; 
#else
		std::cout << v << endl;
#endif
	}
}


void SaseEncoding::generate_operator_variables(int time){
	for (size_t op = 0; op < task_proxy.get_operators().size(); op ++){
		int opvar = sat->new_variable();
		operator_variables[time].push_back(opvar);
		variableCounter["operator"]++;
		DEBUG(sat->registerVariable(opvar,"op " + utils::pad_int(op) + " @ " + utils::pad_int(time) + " " + task_proxy.get_operators()[op].get_name()));
	}
}

void SaseEncoding::encode(int fromTime, int toTime) {
	curClauseNumber = sat->get_number_of_clauses();
	//////////////////////////////////////////////////////////////////
	////////////// 1. Generate all base variables (actions and facts)

	// variables representing operators
	if (operator_variables.count(fromTime) == 0) generate_operator_variables(fromTime);
	DEBUG(log << "New Operator Variables Generated " << sat->number_of_variables<< endl);
	
	

	if (forceAtLeastOneAction) sat->atLeastOne(operator_variables[fromTime]);

	registerClauses("action control");
}

//////////////////////////////////////////////////////////////////
// Initial state
void SaseEncoding::encodeInit(int fromTime, bool retractable) {
	State init = task_proxy.get_initial_state();
	init.unpack();
	for (size_t i = 0; i < init.size(); i++){

	}
	registerClauses("init");
}


//////////////////////////////////////////////////////////////////
// Goal state
void SaseEncoding::encodeGoal(int toTime, bool retractable) {
	GoalsProxy goals = task_proxy.get_goals();
	for (size_t i = 0; i < goals.size(); i++){
		
	}
	registerClauses("goal");

	//////////////////////////////////////////////////////////////////
	// Generation of formula done
	//DEBUG(sat->printVariables());


	// print variable and clause statistics
	log << "Variables" << setw(23) << setfill(' ') << "total: " << setw(8) << setfill(' ') << sat->number_of_variables << endl;
	for (auto [a,b] : variableCounter)
		log << setw(30) << setfill(' ') << a << ": " << setw(8) << setfill(' ') << b << endl;
	log << "Clauses" << setw(25) << setfill(' ') << "total: " << setw(8) << setfill(' ') << sat->get_number_of_clauses() << endl;
	for (auto [a,b] : clauseCounter)
		log << setw(30) << setfill(' ') << a << ": " << setw(8) << setfill(' ') << b << endl;

}


void SaseEncoding::encodeStateEquals(int fromTime, int toTime, bool retractable){

}


std::tuple<State,std::vector<State>,Plan> SaseEncoding::extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) {
	//printVariableTruth(solver,capsule);

	// maps operator to their index in the global ordering
	std::vector<int> global_action_indexing(task_proxy.get_operators().size());
	for(size_t i = 0; i < factory->global_action_ordering.size(); i++)
		global_action_indexing[factory->global_action_ordering[i]] = i;

	map<int,int> planPositionsToSATStates;
	planPositionsToSATStates[0] = 0;
	Plan plan;

	// plan extraction
	for (auto [time,toTime] : time_step_order){
		map<int,int> operatorsThisTime;
		for (size_t op = 0; op < task_proxy.get_operators().size(); op++){
			// the leaf operators don't have to be inserted into the plan
			int opvar = operator_variables[time][op];
			int val = ipasir_val(sat->solver,opvar);
			if (val > 0){
				operatorsThisTime[global_action_indexing[op]] = op;
				//DEBUG(log << "time " << time << " operator " << task_proxy.get_operators()[op].get_name() << endl);
				//log << "kisvar " << opvar << " " << kissat_import_literal((kissat*)solver,opvar) << endl;
			}
		}

		// sort the operators according to their global sorting
		for (auto & [_sortkey, op] : operatorsThisTime){
			plan.push_back(OperatorID(op));
			log << "time " << time << " Event: " << plan.size() << " sorted operator " << task_proxy.get_operators()[op].get_name() << endl;
		}

		planPositionsToSATStates[plan.size()] = time + 1;
	}
    
	//for(int time = 0; time <= currentLength; time++){
	//	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
	//		if (var >= 2) continue;
	//		VariableProxy varProxy = task_proxy.get_variables()[var];
	//		for (int val = 0; val < varProxy.get_domain_size(); val++){
	//			int factVar = fact_variables[time][var][val];
	//			if (ipasir_val(solver,factVar) > 0){
	//				log << "time " << time << " " <<varProxy.get_name() << "=" <<  varProxy.get_fact(val).get_name() << endl;
	//			}
	//		}
	//	}
	//}

	OperatorsProxy operators = task_proxy.get_operators();
	State cur = task_proxy.get_initial_state();
	vector<State> visited_states;
	visited_states.push_back(cur);
	
	for (size_t i = 0; i < plan.size(); ++i) {
	    cur = cur.get_unregistered_successor(operators[plan[i]]);
	    visited_states.push_back(cur);
	}
   

	return make_tuple(visited_states.back(),visited_states,plan);	
};

// functions for debugging
void SaseEncoding::assertLabelsAtTime(int fromTime, std::set<int> labels) {

};

}
