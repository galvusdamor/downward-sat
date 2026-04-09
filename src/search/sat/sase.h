#ifndef SASE_H
#define SASE_H

#include "sat_encoder.h"
#include "sat_encoding.h"

#include <memory>
#include <vector>
#include <map>
#include <set>


namespace sat_search {

// abstract interface for initialisation of SAT encoding
class SaseEncodingFactory : public SATEncodingFactory{

public:
	SaseEncodingFactory(
			bool forceAtLeastOneAction):
		SATEncodingFactory(forceAtLeastOneAction){ };
			
	virtual std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
	virtual void initialize(const TaskProxy _task_proxy, utils::LogProxy _log) override;
};

class SaseEncoding : public SATEncoding {
public:
	SaseEncodingFactory* factory;


	// dynamic variables generated while creating the formula
	// index: timestep -> operator 
	std::map<int,std::vector<int>> operator_variables;

	void printVariableTruth();
	
	int curClauseNumber;
	std::map<std::string,int> clauseCounter;
	std::map<std::string,int> variableCounter;
	
	
	void generate_operator_variables(int time);

public:
    mutable utils::LogProxy log;
	
	SaseEncoding(
		SaseEncodingFactory* _factory,
		std::shared_ptr<sat_capsule> capsule, const TaskProxy _task_proxy, bool forceAtLeastOneAction,
    	utils::LogProxy _log);


	void encode(int fromTime, int toTime) override;
	void encodeInit(int fromTime, bool retractable) override;
	void encodeGoal(int toTime, bool retractable) override;
	void encodeStateEquals(int fromTime, int toTime, bool retractable) override;
	std::tuple<State,std::vector<State>,Plan> extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;
	
	// functions for debugging
	void assertLabelsAtTime(int fromTime, std::set<int> labels) override;
};



//extern void add_sat_search_options_to_feature(plugins::Feature &feature, const std::string &description);
//extern std::tuple<OperatorCost, int, double, std::string, utils::Verbosity>
//get_sat_search_arguments_from_options(const plugins::Options &opts);
}

#endif
