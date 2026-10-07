#ifndef KAUTZ_SELMAN_RINTANEN_H
#define KAUTZ_SELMAN_RINTANEN_H

#include "base_encoding.h"

#include <memory>
#include <vector>
#include <map>
#include <set>


namespace sat_search {

struct AxiomSCC{
	std::vector<int> variables;
	bool sizeOne = false;
	bool isOfImplicationType = false;
	bool isDependentOnOneVariableInternally = false;
	int dependingVariable = false;


	bool fullComputationRequired = false;
	int numberOfAxiomLayers;

	// preprocessing information implications
	std::vector<std::vector<int>> directTransitiveImplications;
	std::vector<std::vector<int>> directTransitiveCauses;

	// preprocessing information guarded implications (i.e. ones that depend on a variable value)
	std::vector<std::vector<std::vector<int>>> guardedTransitiveImplications;
	std::vector<std::vector<std::vector<int>>> guardedTransitiveCauses;
	
};

class KautzSelmanRintanenEncodingFactory : public BaseEncodingFactory {
public:
	// TODO fill and use this map
	std::map<int,std::map<int,int>> operator_precondition_map;	

	// axiom structure graph
	std::vector<std::vector<int>> derived_implication;
	std::vector<std::vector<int>> pos_derived_implication;
	std::vector<std::vector<int>> neg_derived_implication;
	std::map<FactPair, std::vector<int>> derived_entry_edges;
	void axiom_dfs(int var, std::set<int> & posReachable, std::set<int> & negReachable, bool mode);

	// axiom SCCs
	std::vector<AxiomSCC> axiomSCCsInTopOrder;
	std::map<int,int> numberOfAxiomLayerVariablesPerDerived;
	std::vector<std::vector<OperatorProxy>> achievers_per_derived;

	// exists step
	// generate Erasing and Requiring list
	// per fact, per group (SCC of the disabling graph), gives a list of all E/R as a pair: <operator,position_in_group>
	std::map<FactPair,std::vector< std::vector<std::pair<int,int>>  >> erasingList;
	std::map<FactPair,std::vector< std::vector<std::pair<int,int>>  >> requiringList;

	std::pair<std::vector<FactPair>, std::vector<FactPair> > compute_needs_and_deletes_for_operator(int op);

private:
	int encoding;

	// only needed between computing the ordering and preparing the encoding
	InterferenceInfo interference;
	ActionOrderingResult ordering_result;

	void set_up_axioms();
	void set_up_exists_step();
	void set_up_single_step();
	void build_erasing_and_requiring_lists();

protected:
	void analyse_task() override;
	void compute_action_ordering() override;
	void prepare_encoding() override;

public:
	KautzSelmanRintanenEncodingFactory(
			int _encoding,
			std::shared_ptr<ActionOrdering> _ordering,
			const std::string &_write_ordering_file,
			bool _stop_after_ordering,
			bool _encode_fdr_mutexes,
			bool _encode_additional_mutexes,
			bool forceAtLeastOneAction):
		BaseEncodingFactory(_ordering, _write_ordering_file, _stop_after_ordering,
			_encode_fdr_mutexes, _encode_additional_mutexes, forceAtLeastOneAction),
		encoding(_encoding)
		{ };
			
	virtual std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
};

class KautzSelmanRintanenEncoding : public BaseEncoding {
public:
	KautzSelmanRintanenEncodingFactory* factory;
	
	// debugging / output configuration
	bool logInference = false;
	
	// actual run configuration
	bool existsStep = true;

	// index: timestep -> variable
	std::map<int,std::vector<std::vector<int>>> axiom_variables;
	int get_axiom_var(int time, int layer, FactProxy fact);
	int get_axiom_var(int time, int layer, FactPair fact);
	int get_last_axiom_var(int time, FactProxy fact);
	int get_last_axiom_var(int time, FactPair fact);

	// index: timestep -> variable -> value -> list of causes
	std::map<int,std::vector<std::vector<std::vector<int>>>> achieverVars;
	std::map<int,std::vector<std::vector<std::vector<int>>>> deleterVars;


	void axiom_encoding_for_timestep(int time);
	
	void printVariableTruth();



	void exists_step_restriction(std::vector<int> & operator_variables, int time);
	void generateChain(std::vector<int> & operator_variables,
		    const std::vector<std::pair<int, int>>& E, const std::vector<std::pair<int, int>>& R, int time);

	
	void generate_derived_predicate_variables(int time);
	void encode_direct_preconditions_and_effects_of_action(int time,
		std::map<FactPair, std::map<std::set<int>,std::vector<int>>> conditionBuffer,
		std::map<FactPair, std::map<std::set<int>, int>> conditionVariable
			);

	int get_derived_goal_var(int time, FactProxy goal) override;

public:
	KautzSelmanRintanenEncoding(
		KautzSelmanRintanenEncodingFactory* _factory,
		int _encoding,
		std::shared_ptr<sat_capsule> capsule, const TaskProxy _task_proxy, bool forceAtLeastOneAction,
    	utils::LogProxy _log);


	void encode(int fromTime, int toTime) override;
};



//extern void add_sat_search_options_to_feature(plugins::Feature &feature, const std::string &description);
//extern std::tuple<OperatorCost, int, double, std::string, utils::Verbosity>
//get_sat_search_arguments_from_options(const plugins::Options &opts);
}

#endif
