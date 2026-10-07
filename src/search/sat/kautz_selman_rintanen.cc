#include <algorithm>
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



namespace sat_search {

	
void KautzSelmanRintanenEncodingFactory::analyse_task() {
	// set up data structures for derived predicates and axioms -- won't do anything if none of them exist.
	set_up_axioms();
}

void KautzSelmanRintanenEncodingFactory::compute_action_ordering() {
	// prepare for parallelism encodings 
	if (encoding == 2)
		set_up_exists_step();
	else
		set_up_single_step();
}

void KautzSelmanRintanenEncodingFactory::prepare_encoding() {
	if (encoding == 2)
		build_erasing_and_requiring_lists();
}
	
	
std::unique_ptr<SATEncoding> KautzSelmanRintanenEncodingFactory::createEncodingInstance(std::shared_ptr<sat_capsule> capsule){
	return make_unique<KautzSelmanRintanenEncoding>(this,encoding,capsule,*task_proxy,forceAtLeastOneAction,*log);

};

	
	
KautzSelmanRintanenEncoding::KautzSelmanRintanenEncoding(
	KautzSelmanRintanenEncodingFactory* _factory,
	int _encoding,
	std::shared_ptr<sat_capsule> capsule,
	const TaskProxy _task_proxy,
	bool forceAtLeastOneAction,
	utils::LogProxy _log)
	:BaseEncoding(_factory,capsule,_task_proxy,forceAtLeastOneAction,_log),
	factory(_factory)
	{

	switch (_encoding){
		case 0: existsStep = false; break;
		case 2: existsStep = true; break;
		default:
			log << "Error: encoding No " << _encoding << " is not supported" << endl;
			exit(-1);
	}

}


void KautzSelmanRintanenEncodingFactory::axiom_dfs(int var, set<int> & posReachable, set<int> & negReachable, bool mode){
	if (mode){
		// causing fact has become true, this means the DP could turn true
		if (posReachable.count(var)) return;
		posReachable.insert(var);

		// search for all axioms in which this var is contained positively. They could also turn true
		for(int & succ : pos_derived_implication[var])
			axiom_dfs(succ,posReachable, negReachable, true);
		
		// search for all axioms in which this var is contained negatively. They could turn false
		for(int & succ : neg_derived_implication[var])
			axiom_dfs(succ,posReachable, negReachable, false);

	} else {
		// causing fact has become false, this means the DP could turn false
		if (negReachable.count(var)) return;
		negReachable.insert(var);

		// search for all axioms in which this var is contained positively. They could also turn false
		for(int & succ : pos_derived_implication[var])
			axiom_dfs(succ,posReachable, negReachable, false);
		
		// search for all axioms in which this var is contained negatively. They could turn true
		for(int & succ : neg_derived_implication[var])
			axiom_dfs(succ,posReachable, negReachable, true);
	}
}

void KautzSelmanRintanenEncodingFactory::set_up_axioms(){
	derived_implication.clear();
	derived_implication.resize(task_proxy->get_variables().size());
	pos_derived_implication.clear();
	pos_derived_implication.resize(task_proxy->get_variables().size());
	neg_derived_implication.clear();
	neg_derived_implication.resize(task_proxy->get_variables().size());
	achievers_per_derived.resize(task_proxy->get_variables().size());
	derived_entry_edges.clear();

	// find statically true DPs
	AxiomsProxy axioms = task_proxy->get_axioms();
	bool newTrueFound = true;
	while (newTrueFound){
		newTrueFound = false;
		for (size_t ax = 0; ax < axioms.size(); ax++){
			OperatorProxy opProxy = axioms[ax];

			// Effect
			EffectsProxy effs = opProxy.get_effects();
			assert(effs.size() == 1);
			EffectProxy thisEff = effs[0];
			assert(thisEff.get_fact().get_variable().is_derived());
			int eff_var = thisEff.get_fact().get_variable().get_id();
			if (statically_true_derived_predicates.count(eff_var)) continue;
			assert(thisEff.get_fact().get_value() == 1);
			
			// Preconditions
			PreconditionsProxy precs = opProxy.get_preconditions();
			vector<FactProxy> conds;
		
			for (size_t pre = 0; pre < precs.size(); pre++)
				conds.push_back(precs[pre]);
			
			EffectConditionsProxy cond = thisEff.get_conditions();
			for (size_t i = 0; i < cond.size(); i++)
				conds.push_back(cond[i]);

			int number_of_true_conditions = 0;
			bool inApplicable = false;
			for (FactProxy & fact : conds){
				if (fact.get_variable().is_derived() && fact.get_variable().get_id() == eff_var){
					assert(fact.get_value() == 0);
					continue;	
				}
				if (statically_true_derived_predicates.count(fact.get_variable().get_id())){
					if (fact.get_value()) continue;	// condition is always true
					inApplicable = true;
					break;
				}
				
				number_of_true_conditions++;
			}
			if (inApplicable) continue;

			if (number_of_true_conditions == 0){
				DEBUG(*log << "Found statically true derived predicate: " << task_proxy->get_variables()[eff_var].get_name() << endl);
				statically_true_derived_predicates.insert(eff_var);
				newTrueFound = true;
			}
		}
	}
	*log << "Found statically true derived predicates: " << statically_true_derived_predicates.size() << endl;

	// building the derived predicate dependency graph
	for (size_t ax = 0; ax < axioms.size(); ax++){
		OperatorProxy opProxy = axioms[ax];

		// Effect
		EffectsProxy effs = opProxy.get_effects();
		assert(effs.size() == 1);
		EffectProxy thisEff = effs[0];
		assert(thisEff.get_fact().get_variable().is_derived());
		
		int eff_var = thisEff.get_fact().get_variable().get_id();
		assert(thisEff.get_fact().get_value() == 1);
		achievers_per_derived[eff_var].push_back(opProxy);

		// statically true DP, it does not depend on anything even if there are axioms.
		if (statically_true_derived_predicates.count(eff_var)) continue;

		// Preconditions
		PreconditionsProxy precs = opProxy.get_preconditions();
		vector<FactProxy> conds;
	
		for (size_t pre = 0; pre < precs.size(); pre++)
			conds.push_back(precs[pre]);
		
		EffectConditionsProxy cond = thisEff.get_conditions();
		for (size_t i = 0; i < cond.size(); i++)
			conds.push_back(cond[i]);

		for (FactProxy & fact : conds){
			if (fact.get_variable().is_derived()){
				// the variables that is changed will require value 0
				if (fact.get_variable().get_id() == eff_var){
					assert(fact.get_value() == 0);
					continue;	
				}

				//assert(fact.get_value() == 1);
				int fact_var = fact.get_variable().get_id();
				derived_implication[fact_var].push_back(eff_var);

				if (fact.get_value() == 1)
					pos_derived_implication[fact_var].push_back(eff_var);
				else
					neg_derived_implication[fact_var].push_back(eff_var);
			} else {
				derived_entry_edges[fact.get_pair()].push_back(eff_var);
			}
		}
	}


	vector<vector<int>> initial_derived_sccs = sccs::compute_maximal_sccs(derived_implication);
	vector<vector<int>> derived_sccs;
	int numberDerivedPredicates = 0;
	for (vector<int> s : initial_derived_sccs){
		if (s.size() == 1 && !task_proxy->get_variables()[s[0]].is_derived()) continue;
		derived_sccs.push_back(s);
		numberDerivedPredicates += s.size();
		//log << "SCC of size " << s.size() << endl;
	}
	*log << "Number of SCCs " << derived_sccs.size() - statically_true_derived_predicates.size() << endl;
	numberDerivedPredicates -= statically_true_derived_predicates.size();

	int sizeOneSCCs = 0;
	int impliationSCCS = 0;
	int oneFactSCCS = 0;
	int oneVarSCCS = 0;
	int oneFactSCCSInternal = 0;
	int oneVarSCCSInternal = 0;
	int problematicSCCS = 0;

	// for output statistics
	map<string,vector<int>> sizes;

	for (vector<int> s : derived_sccs){
		AxiomSCC thisSCC;
		thisSCC.variables = s;
		if (s.size() == 1){
			// this will include statically true DPs.
			sizeOneSCCs++;
			thisSCC.sizeOne = true;
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["sizeone"].push_back(s.size());
			continue;
		}
		set<int> sset(s.begin(), s.end());

		// check if all internal edges are implications only
		bool implicationOnly = true;
		bool twoAntecedants = false;
		FactProxy actualDependency = *task_proxy->get_variables().get_facts().begin(); //(*task,0,0);
		bool oneActualDependency = true;
		int varDependency = -1;

		FactProxy actualDependencyInternal = *task_proxy->get_variables().get_facts().begin(); //(*task,0,0);
		bool oneActualDependencyInternal = true;
		int varDependencyInternal = -1;
		set<int> dependentVariables;
		for (int dp : s){
			for (OperatorProxy opProxy : achievers_per_derived[dp]){
				// effect
				EffectsProxy effs = opProxy.get_effects();
				EffectProxy thisEff = effs[0];
				int eff_var = thisEff.get_fact().get_variable().get_id();
				// Preconditions
				PreconditionsProxy precs = opProxy.get_preconditions();
				vector<FactProxy> conds;
	
				for (size_t pre = 0; pre < precs.size(); pre++)
					conds.push_back(precs[pre]);
				
				EffectConditionsProxy cond = thisEff.get_conditions();
				for (size_t i = 0; i < cond.size(); i++)
					conds.push_back(cond[i]);

				int numDerived = 0;
				bool hasActual = false;
				FactProxy myActualDependency = *task_proxy->get_variables().get_facts().begin(); //(*task,0,0);
				bool myOneActualDependency = true;
				int myVarDependency = -1;
				
				for (FactProxy & fact : conds){
					if (fact.get_variable().get_id() == eff_var) continue;
					if (fact.get_variable().is_derived() &&
							sset.count(fact.get_variable().get_id())){
						numDerived++;
					} else {
						// condition outside of this SCC or non-derived
						hasActual = true;
						if (myVarDependency == -1){
							myVarDependency = fact.get_variable().get_id();
							myActualDependency = fact;
						} if (myVarDependency != fact.get_variable().get_id()){
							myVarDependency = -2; // dependent on multiple variables
						} else if (myActualDependency != fact){
							myOneActualDependency = false;
						}
						dependentVariables.insert(fact.get_variable().get_id());
					}
					if (!hasActual && numDerived == 1) continue;
					if (hasActual && numDerived == 0) continue;
					//log << "FAIL SCC with " << fact.get_variable().get_id() << " -> " << eff_var << endl;
					implicationOnly = false;
				}
				if (myVarDependency != -1){
					if (myVarDependency == -2){
						varDependency = -2;
						oneActualDependency = false;
					} else if (myVarDependency != varDependency){
						// no dependency known before
						if (varDependency == -1){
							varDependency = myVarDependency;
							actualDependency = myActualDependency;
						} else {
							varDependency = -2;
							oneActualDependency = false;
						}
					} else if (!myOneActualDependency || myActualDependency != actualDependency){
						oneActualDependency = false;
					}

					if (numDerived){
						if (myVarDependency == -2){
							varDependencyInternal = -2;
							oneActualDependencyInternal = false;
						} else if (myVarDependency != varDependencyInternal){
							// no dependency known before
							if (varDependencyInternal == -1){
								varDependencyInternal = myVarDependency;
								actualDependencyInternal = myActualDependency;
							} else {
								varDependencyInternal = -2;
								oneActualDependencyInternal = false;
							}
						} else if (!myOneActualDependency || myActualDependency != actualDependencyInternal){
							oneActualDependencyInternal = false;
						}
					}
				}
				if (numDerived >= 2) twoAntecedants = true;
			}
		}

		if (twoAntecedants){
			*log << "Problematic (2 antecedants) SCC of size " << s.size() << endl;
			problematicSCCS++;
			thisSCC.fullComputationRequired = true;
			thisSCC.numberOfAxiomLayers = thisSCC.variables.size();
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["problematic-2ante"].push_back(s.size());
			continue;
		}

		if (implicationOnly){
			*log << "Implication SCC of size " << s.size() << endl;
			impliationSCCS++;
			thisSCC.isOfImplicationType = true;
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["implication"].push_back(s.size());
			continue;
		}

		if (oneActualDependency){
			*log << "One fact dependency SCC of size " << s.size() << endl;
			oneFactSCCS++;
			thisSCC.isDependentOnOneVariableInternally = true;
			thisSCC.dependingVariable = varDependencyInternal;
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["onefact"].push_back(s.size());
			continue;
		}

		if (oneActualDependencyInternal){
			*log << "One fact dependency internal SCC of size " << s.size() << endl;
			oneFactSCCSInternal++;
			thisSCC.isDependentOnOneVariableInternally = true;
			thisSCC.dependingVariable = varDependencyInternal;
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["onefactinternal"].push_back(s.size());
			continue;
		}

		if (varDependency != -2){
			*log << "One var dependency SCC of size " << s.size() << endl;
			oneVarSCCS++;
			thisSCC.isDependentOnOneVariableInternally = true;
			thisSCC.dependingVariable = varDependencyInternal;
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["onevar"].push_back(s.size());
			continue;
		}

		if (varDependencyInternal != -2){
			*log << "One var dependency internal SCC of size " << s.size() << endl;
			oneVarSCCSInternal++;
			thisSCC.isDependentOnOneVariableInternally = true;
			thisSCC.dependingVariable = varDependencyInternal;
			axiomSCCsInTopOrder.push_back(thisSCC);
			sizes["onevarinternal"].push_back(s.size());
			continue;
		}


		int combiSize = 1;
		for (const int & v : dependentVariables){
			//log << "Var " << v << " size: " << task_proxy->get_variables()[v].get_domain_size() << endl;
			combiSize *= task_proxy->get_variables()[v].get_domain_size();
		}
		*log << "SCC size " << s.size() << " Dependent variables: " << dependentVariables.size() << " Combi size: " << combiSize << endl;

		//log << "Problematic SCC of size " << s.size() << endl;
		//log << "members:";
		//for (int d : sset) log << d << " ";
		//log << endl;
		problematicSCCS++;
		
		thisSCC.fullComputationRequired = true;
		thisSCC.numberOfAxiomLayers = thisSCC.variables.size();
		axiomSCCsInTopOrder.push_back(thisSCC);
		sizes["problematic-general"].push_back(s.size());
	}
	*log << "Size 1 SCCS: " << sizeOneSCCs << endl;
	*log << "Implication SCCS: " << impliationSCCS << endl;
	*log << "OneFact SCCS: " << oneFactSCCS << endl;
	*log << "OneVar SCCS: " << oneVarSCCS << endl;
	*log << "OneFact internal SCCS: " << oneFactSCCSInternal << endl;
	*log << "OneVar internal SCCS: " << oneVarSCCSInternal << endl;
	*log << "Other SCCS: " << problematicSCCS << endl;
	
	// statistics for SCCs
	for (auto [name,ss] : sizes){
		int minSize = task_proxy->get_variables().size(); 
		int maxSize = 0;
		int sumSize = 0;
		sort(ss.begin(), ss.end());
		int median = ss[ss.size() / 2];
		int number = ss.size();

		for (int size : ss){
			minSize = (size < minSize)? size : minSize;
			maxSize = (size > maxSize)? size : maxSize;
			sumSize += size;
		}

		if (name == "sizeone"){
			sumSize -= statically_true_derived_predicates.size(); 	
			number -= statically_true_derived_predicates.size(); 	
		}

		if (number == 0) continue;
		
		*log << name << " number_sccs: " << number;
		*log << " minsize: " << minSize;
		*log << " maxsize: " << maxSize;
		*log << " sumsize: " << sumSize << " percent_of_all: " <<
		*	fixed << setprecision(5) << double(sumSize) / numberDerivedPredicates;
		*log << " median: " << median;
		*log << " average: " << fixed << setprecision(5) << double(sumSize) / number;
		*log << endl;
	}
	*log << "statically_true" << " number: " << statically_true_derived_predicates.size() <<
	   " percent_of_all: " << fixed << setprecision(5) <<
	  	 double(statically_true_derived_predicates.size()) / 
		 (statically_true_derived_predicates.size() + numberDerivedPredicates) << endl;


	// pre-process the axiom SCCs that can be handled specially
	for (AxiomSCC &scc : axiomSCCsInTopOrder){
		map<int,int> varsToOffset;
		for (size_t varOffset = 0; varOffset < scc.variables.size(); varOffset++)
			varsToOffset[scc.variables[varOffset]] = varOffset;

		if (scc.isOfImplicationType){
			*log << "Pre Processing Implication Type SCC" << endl;

			// run Floyd Warshall on the implications
			vector<vector<bool>> reach (scc.variables.size());
			for (size_t varOffset = 0; varOffset < scc.variables.size(); varOffset++){
				reach[varOffset].resize(scc.variables.size());
				reach[varOffset][varOffset] = true;
				for (int implicated : derived_implication[scc.variables[varOffset]]){
					if (varsToOffset.count(implicated))
						reach[varOffset][varsToOffset[implicated]] = true;
				}
			}
			
			// compute transitive closure 
			for (size_t k = 0; k < reach.size(); k++)
				for (size_t i = 0; i < reach.size(); i++)
					for (size_t j = 0; j < reach.size(); j++)
						if (reach[i][k] && reach[k][j]) reach[i][j] = true;


			scc.directTransitiveImplications.resize(scc.variables.size());
			scc.directTransitiveCauses.resize(scc.variables.size());
			for (size_t varOffset = 0; varOffset < reach.size(); varOffset++)
				for (size_t varOffsetTo = 0; varOffsetTo < reach.size(); varOffsetTo++)
					if (reach[varOffset][varOffsetTo]){
						scc.directTransitiveImplications[varOffset].push_back(varOffsetTo);
						scc.directTransitiveCauses[varOffsetTo].push_back(varOffset);
					}
			
			//for (size_t varOffset = 0; varOffset < reach.size(); varOffset++){
			//	log << "Variable " << scc.variables[varOffset] << " reaches:";
			//	for (int offset : scc.directTransitiveImplications[varOffset])
			//		log << " " << scc.variables[offset];
			//	log << endl;
			//}

		} else if (scc.isDependentOnOneVariableInternally){
			*log << "Pre Processing One Variable Dependent SCC" << endl;
			
			VariableProxy varProxy = task_proxy->get_variables()[scc.dependingVariable];
			scc.guardedTransitiveImplications.resize(varProxy.get_domain_size());
			scc.guardedTransitiveCauses.resize(varProxy.get_domain_size());

			for (int value = 0; value < varProxy.get_domain_size(); value++){
				// run Floyd Warshall on the implications
				vector<vector<bool>> reach (scc.variables.size());
				for (size_t varOffset = 0; varOffset < scc.variables.size(); varOffset++){
					reach[varOffset].resize(scc.variables.size());
					reach[varOffset][varOffset] = true;
				}
				
				for (size_t varOffsetTo = 0; varOffsetTo < scc.variables.size(); varOffsetTo++){
					int variableTo = scc.variables[varOffsetTo];
					for (OperatorProxy opProxy : achievers_per_derived[variableTo]){
						// effect
						EffectsProxy effs = opProxy.get_effects();
						assert(effs.size() == 1);
						EffectProxy thisEff = effs[0];
						assert(thisEff.get_fact().get_value() == 1);
						assert(thisEff.get_fact().get_variable().is_derived());
						int eff_var = thisEff.get_fact().get_variable().get_id();
						// Preconditions
						PreconditionsProxy precs = opProxy.get_preconditions();
						vector<FactProxy> conds;
		
						for (size_t pre = 0; pre < precs.size(); pre++)
							conds.push_back(precs[pre]);
						
						EffectConditionsProxy cond = thisEff.get_conditions();
						for (size_t i = 0; i < cond.size(); i++)
							conds.push_back(cond[i]);
	
						int numDerived = 0;
						FactProxy myActualDependency = *task_proxy->get_variables().get_facts().begin(); //(*task,0,0);
						int myVarDependency = -1;
						int derivedDependency = -1;
						for (FactProxy & fact : conds){
							if (fact.get_variable().get_id() == eff_var) continue;
							if (fact.get_variable().is_derived() &&
									varsToOffset.count(fact.get_variable().get_id())){
								numDerived++;
								derivedDependency = fact.get_variable().get_id();
							} else {
								// condition outside of this SCC or non-derived
								if (myVarDependency == -1){
									myVarDependency = fact.get_variable().get_id();
									myActualDependency = fact;
								} if (myVarDependency != fact.get_variable().get_id()){
									myVarDependency = -2; // dependent on multiple variables
								} else if (myActualDependency != fact){
									assert(false); // this cannot happen
								}
							}
						}

						// axiom that is not internal to the SCC, will be handled separately
						if (numDerived == 0) continue; 
						assert(myVarDependency != -2);
						if (myVarDependency == -1){
							// this is a simple implication without condition, so it is always reached
							reach[varsToOffset[derivedDependency]][varOffsetTo] = true;
						} else {
							assert(myVarDependency == scc.dependingVariable);
							// not the current value
							if (value != myActualDependency.get_value()) continue;
							reach[varsToOffset[derivedDependency]][varOffsetTo] = true;
						}
					}
				}

				
				// compute transitive closure 
				for (size_t k = 0; k < reach.size(); k++)
					for (size_t i = 0; i < reach.size(); i++)
						for (size_t j = 0; j < reach.size(); j++)
							if (reach[i][k] && reach[k][j]) reach[i][j] = true;


				scc.guardedTransitiveImplications[value].resize(scc.variables.size());
				scc.guardedTransitiveCauses[value].resize(scc.variables.size());
				for (size_t varOffset = 0; varOffset < reach.size(); varOffset++)
					for (size_t varOffsetTo = 0; varOffsetTo < reach.size(); varOffsetTo++)
						if (reach[varOffset][varOffsetTo]){
							scc.guardedTransitiveImplications[value][varOffset].push_back(varOffsetTo);
							scc.guardedTransitiveCauses[value][varOffsetTo].push_back(varOffset);
						}
				
				//for (size_t varOffset = 0; varOffset < reach.size(); varOffset++){
				//	log << "Variable " << scc.variables[varOffset] << " reaches with value " << value << ":";
				//	for (int offset : scc.guardedTransitiveImplications[value][varOffset])
				//		log << " " << scc.variables[offset];
				//	log << endl;
				//}
			}
		}
	}

	// axiom set up
	for (AxiomSCC & scc : axiomSCCsInTopOrder){
		if (scc.sizeOne) scc.numberOfAxiomLayers = 1;

		// nasty case. We can't optimise here
		if (scc.fullComputationRequired){
			for (int v : scc.variables){
				numberOfAxiomLayerVariablesPerDerived[v] = scc.numberOfAxiomLayers;
			}
		} else {
			for (int v : scc.variables)
				numberOfAxiomLayerVariablesPerDerived[v] = 1;
		}
	}
	
	DEBUG(*log << "Axiom SCC number calculated" << endl);
}

void KautzSelmanRintanenEncodingFactory::set_up_single_step() {
	for (size_t op = 0; op < task_proxy->get_operators().size(); op++)
		global_action_ordering.push_back(op);
}



// Function computes for a given operator, all facts that it
// 1) might need. These are preconditions and all conditions of conditional effects
// 2) it might delete. This includes derived predicates that it might delete
pair<vector<FactPair>, vector<FactPair> > KautzSelmanRintanenEncodingFactory::compute_needs_and_deletes_for_operator(int op){
	vector<FactPair> needs;
	vector<FactPair> deletes;
	OperatorProxy opProxy = task_proxy->get_operators()[op];
	PreconditionsProxy precs = opProxy.get_preconditions();

	// preconditions as a map. Maps variable to its value required by a precondition.
	// Only necessary for correct evaluation of axioms
	map<int,int> preMap;
	vector<FactPair> fullPreConditions;
	for (size_t pre = 0; pre < precs.size(); pre++){
		FactProxy fact = precs[pre];
		needs.push_back(fact.get_pair());
		preMap[fact.get_variable().get_id()] = fact.get_value();
		fullPreConditions.push_back(fact.get_pair());
	}

	// loop over effects
	EffectsProxy effs = opProxy.get_effects();
	for (size_t eff = 0; eff < effs.size(); eff++){
		EffectProxy thisEff = effs[eff];
		// gather the conditions of the conditional effect 
		EffectConditionsProxy cond = thisEff.get_conditions();
		vector<FactPair> conditions;
		vector<FactPair> fullConditions = fullPreConditions;
		for (size_t i = 0; i < cond.size(); i++){
			FactProxy condition = cond[i];
			// conditions of conditional effects might be needed. For disabling graph, we need to be cautious
			needs.push_back(condition.get_pair());
			conditions.push_back(condition.get_pair());
			fullConditions.push_back(condition.get_pair());
		}
		

		// axioms only
		// setting a fact to true can cause a DP to become true, which in turn means we make a precondition that it has to be false
		for (int & start : derived_entry_edges[thisEff.get_fact().get_pair()]){
			set<int> posReachable, negReachable;
			axiom_dfs(start,posReachable, negReachable, true); // fact has become true
			for (const int & reach : posReachable){
				deletes.push_back(FactPair(reach,0));
			}
			for (const int & reach : negReachable){
				deletes.push_back(FactPair(reach,1));
			}
		}



		// implicit deleting effects, i.e. delete any value of the variable that is set
		for (int val = 0; val < thisEff.get_fact().get_variable().get_domain_size(); val++){
			// an effect var=v implicitly deletes all other facts var=v' with v != v'
			if (val == thisEff.get_fact().get_value()) continue;
			// -- unless the variable var must have had a different value than v' before the action was executed.
			// TODO: one can also check the condition of the conditional effect here!
			// currently we check only the actual preconditions of the action. Can help for cases where we have an effect
			// v=1 -> v=2
			if (preMap.count(thisEff.get_fact().get_variable().get_id()) &&
				preMap[thisEff.get_fact().get_variable().get_id()] != val)
				continue;

			FactPair deletedFact(thisEff.get_fact().get_variable().get_id(),val);
			deletes.push_back(deletedFact);

			// treat operators that have an effect that can make a derived fact false as if they were deletes of that fact
			for (int & start : derived_entry_edges[deletedFact]){
				set<int> posReachable, negReachable;
				axiom_dfs(start,posReachable, negReachable, false); // fact has become true
				// if we make the entry point true, any of the connected axioms might become true,
				// so we might delete any negative precondition on it
				for (const int & reach : posReachable)
					deletes.push_back(FactPair(reach,0));
				for (const int & reach : negReachable)
					deletes.push_back(FactPair(reach,1));
			}
		}
	}
	return make_pair(needs,deletes);
}


void KautzSelmanRintanenEncodingFactory::set_up_exists_step() {
	/////////// Exists step encoding
	// compute which operators need and delete which facts
	for(size_t op = 0; op < task_proxy->get_operators().size(); op ++){
		if (op % 1000 == 0)
			*log << "Disabling Graph Operator " << op << " of " << task_proxy->get_operators().size() << endl;
		auto [needs, deletes] = compute_needs_and_deletes_for_operator(op);
		for (const FactPair & f : needs) interference.needing[f].insert(op);
		for (const FactPair & f : deletes) interference.deleting[f].insert(op);
	}

	// the ordering (by default the one based on the SCCs of the disabling graph)
	ordering_result = compute_ordering(interference);
	global_action_ordering = ordering_result.order;
}

void KautzSelmanRintanenEncodingFactory::build_erasing_and_requiring_lists() {
	const vector<vector<int>> & groups = ordering_result.groups;
	// group and position in the group for every operator
	vector<int> group_of_operator(task_proxy->get_operators().size(), -1);
	vector<int> position_in_group(task_proxy->get_operators().size(), -1);
	for (size_t group = 0; group < groups.size(); group++){
		// no disabling possible in groups of size one.
		if (groups[group].size() == 1) continue;
		for (size_t pos = 0; pos < groups[group].size(); pos++){
			group_of_operator[groups[group][pos]] = group;
			position_in_group[groups[group][pos]] = pos;
		}
	}

	auto add_to_list = [&](const set<int> & operators, vector<vector<pair<int,int>>> & list){
		for (int op : operators){
			if (group_of_operator[op] == -1) continue;
			list[group_of_operator[op]].push_back({op, position_in_group[op]});
		}
		// sort by position in the group
		for (auto & groupList : list)
			sort(groupList.begin(), groupList.end(),
				[](const pair<int,int> & a, const pair<int,int> & b){return a.second < b.second;});
	};

	for (auto & [factPair, deleters] : interference.deleting){
		if (ordering_result.unconstrained_facts.count(factPair)) continue; // don't need to generate anything for these
		erasingList[factPair].resize(groups.size());
		requiringList[factPair].resize(groups.size());
		add_to_list(deleters, erasingList[factPair]);
		auto needers = interference.needing.find(factPair);
		if (needers != interference.needing.end())
			add_to_list(needers->second, requiringList[factPair]);
	}

	// not needed any more
	interference = InterferenceInfo();
	ordering_result = ActionOrderingResult();
	*log << "Build enabling disabling lists." << std::endl;
}

int chain_number = 0;

void KautzSelmanRintanenEncoding::generateChain(vector<int> & operator_variables,
	const std::vector<std::pair<int, int>>& E,
	const std::vector<std::pair<int, int>>& R,
	int time){
	chain_number++;
	// generate chain variables
	map<int,int> chainVars; // we only need them for every R.second value
	for (const auto& [_ignore, i] : R) {
		int chainVar = sat->new_variable();
		chainVars[i] = chainVar;
		variableCounter["chain"]++;
		// TODO don't have enough information to generate nice names here
		// need: timestep, scc number, fact pair
		DEBUG(sat->registerVariable(chainVar,"chain No " + to_string(chain_number) +  " R " + to_string(i) + " @ " + to_string(time)));
	}

	// Generate chain restriction for every SCC
	size_t rpos = 0;
	for (const auto& [opID, i] : E) {
		// search for the position in the R list with the next higher i value
		while (rpos < R.size() && R[rpos].second <= i)
			rpos++;

		if (rpos < R.size()) sat->implies(operator_variables[opID], chainVars[R[rpos].second]);
	}

	// Generate negative implications in the chain for better unit propagation 
	size_t epos = 0;
	int priorChainVar = -1;
	for (const auto& [opID, i] : R) {
		vector<int> erasersInBetween;
		if (priorChainVar != -1) erasersInBetween.push_back(priorChainVar);
		priorChainVar = chainVars[i];
		// search for the position in the E list 
		while (epos < E.size() && E[epos].second < i){
			erasersInBetween.push_back(operator_variables[E[epos].first]);
			epos++;
		}
		
		sat->allNotImpliesNot(erasersInBetween, priorChainVar);
	}

	// Process R and generate additional clauses
	if (R.size() >= 2){
		for (size_t i = 0; i < R.size() - 1; i++) {
			sat->implies(chainVars[R[i].second], chainVars[R[i+1].second]);
		}
	}

	for (const auto& [opID, i] : R) {
		sat->impliesNot(chainVars[i], operator_variables[opID]);
	}
}

void KautzSelmanRintanenEncoding::exists_step_restriction(vector<int> & operator_variables, int time){
	// loop over all fact pairs
	for (auto & [factPair, requiringLists] : factory->requiringList){
		for (size_t scc = 0; scc < requiringLists.size(); scc++){
			assert(factory->erasingList.count(factPair));
			const std::vector<std::pair<int,int>> & E = factory->erasingList[factPair][scc];
			const std::vector<std::pair<int,int>> & R = requiringLists[scc];

			// no chain to be generated
			if (E.size() == 0 || R.size() == 0) continue;
			if (R.size() == 1 && E.size() == 1 && E[0].first == R[0].first) continue;
			generateChain(operator_variables,E,R, time);
		}
	}
}


int KautzSelmanRintanenEncoding::get_axiom_var(int time, int layer, FactProxy fact){
	assert(factory->statically_true_derived_predicates.count(fact.get_variable().get_id()) == 0);
	assert(fact.get_value() == 1);
	return axiom_variables[time][fact.get_variable().get_id()][layer];
}

int KautzSelmanRintanenEncoding::get_last_axiom_var(int time, FactProxy fact){
	assert(factory->statically_true_derived_predicates.count(fact.get_variable().get_id()) == 0);
	if (fact.get_value() == 1)
		return axiom_variables[time][fact.get_variable().get_id()].back();
	else
		return -axiom_variables[time][fact.get_variable().get_id()].back();
}


int KautzSelmanRintanenEncoding::get_last_axiom_var(int time, FactPair fact){
	if (fact.value == 1)
		return axiom_variables[time][fact.var].back();
	else
		return -axiom_variables[time][fact.var].back();
}

void KautzSelmanRintanenEncoding::printVariableTruth(){
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

void KautzSelmanRintanenEncoding::axiom_encoding_for_timestep(int time){
	// final value of the axioms implies their value for the next layer
	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
		if (task_proxy.get_variables()[var].is_derived() && factory->statically_true_derived_predicates.count(var) == 0){
			// if axiom evaluates to true, its value 1 is the correct one
			sat->implies(axiom_variables[time][var].back(),fact_variables[time][var][1]);
			// if axiom evaluates to false, its value 0 is the correct one
			sat->implies(-axiom_variables[time][var].back(),fact_variables[time][var][0]);
		}
	}
	registerClauses("axioms finalisation");


	// actual evaluation of axioms

	int sccCount = 0;
	for (AxiomSCC & scc : factory->axiomSCCsInTopOrder){
		DEBUG(if (sccCount && sccCount % 1000 == 0){
			log << "Generated Axiom SCC " << sccCount << " of " << factory->axiomSCCsInTopOrder.size() << endl;
		});
		sccCount++;
		set<int> sset(scc.variables.begin(), scc.variables.end());
		if (scc.sizeOne) {
			scc.numberOfAxiomLayers = 1;
			if (factory->statically_true_derived_predicates.count(scc.variables[0])){
				continue; // don't need to generate anything for this variable
			}
		}

		// nasty case. We can't optimise here
		if (scc.sizeOne || scc.fullComputationRequired){
			// initially all axioms are false
			for (int var : scc.variables){
				if (task_proxy.get_variables()[var].is_derived()){
					if (factory->statically_true_derived_predicates.count(var))
						sat->assertYes(axiom_variables[time][var][0]);
					else
						sat->assertNot(axiom_variables[time][var][0]);
				}
			}
			registerClauses("axioms initialisation");

			for (int layer = 0; layer < scc.numberOfAxiomLayers; layer++){
				vector<vector<int>> causeVariables (task_proxy.get_variables().size());
				for (int sccvar : scc.variables){
					//if (factory->statically_true_derived_predicates.count(sccvar)) {
					//	assertYes(solver,axiom_variables[time][sccvar][layer+1]);
					//	continue;
					//}

					// positive maintenance, if DP was true, it must remain true.
					int scc_var_fact = axiom_variables[time][sccvar][layer+1]; 
					int scc_var_fact_cur = axiom_variables[time][sccvar][layer]; 
					sat->implies(scc_var_fact_cur,scc_var_fact);
					causeVariables[sccvar].push_back(scc_var_fact_cur);
					registerClauses("axioms evaluation");

					for (OperatorProxy opProxy : factory->achievers_per_derived[sccvar]){
						// Effect
						EffectsProxy effs = opProxy.get_effects();
						assert(effs.size() == 1);
						EffectProxy thisEff = effs[0];
						assert(thisEff.get_fact().get_value() == 1);
						assert(thisEff.get_fact().get_variable().is_derived());
						int eff_var = thisEff.get_fact().get_variable().get_id();
						assert(eff_var == sccvar);
						assert(factory->statically_true_derived_predicates.count(eff_var) == 0);
						int eff_fact_var = get_axiom_var(time,layer+1,thisEff.get_fact());
						assert(eff_fact_var == scc_var_fact);

						set<int> conditions;
						// Preconditions
						PreconditionsProxy precs = opProxy.get_preconditions();
						vector<FactProxy> conds;
					
						for (size_t pre = 0; pre < precs.size(); pre++)
							conds.push_back(precs[pre]);
						
						EffectConditionsProxy cond = thisEff.get_conditions();
						for (size_t i = 0; i < cond.size(); i++)
							conds.push_back(cond[i]);

						bool inApplicable = false;
						for (FactProxy & fact : conds){
							if (fact.get_variable().is_derived()){
								// the variables that is changed will require value 0
								if (fact.get_variable().get_id() == eff_var){
									assert(fact.get_value() == 0);
									continue;	
								}

								if (factory->statically_true_derived_predicates.count(fact.get_variable().get_id())){
									if (fact.get_value()) continue;	// condition is always true
									inApplicable = true;
									break;
								}

								//assert(fact.get_value() == 1);
								int fact_var;
								if (sset.count(fact.get_variable().get_id()))
									fact_var = get_axiom_var(time,layer,fact);
								else
									fact_var = get_last_axiom_var(time,fact);
								conditions.insert(fact_var);
							} else {
								int fact_var = get_fact_var(time,fact);
								conditions.insert(fact_var);
							}
						}

						if (inApplicable) continue;
						
						sat->andImplies(conditions,eff_fact_var);
						registerClauses("axioms evaluation");

						assert(conditions.size() > 0);
						if (conditions.size() == 1){
							causeVariables[eff_var].push_back(*conditions.begin());
						} else {
							int intermediateCausation = sat->new_variable();
							variableCounter["axiom causation"]++;
							causeVariables[eff_var].push_back(intermediateCausation);
							DEBUG(sat->registerVariable(intermediateCausation,"ca " + utils::pad_int(opProxy.get_id()) + " @ " + utils::pad_int(time) + " " + utils::pad_int(layer)));
							for (int required : conditions)
								sat->implies(intermediateCausation, required);
							registerClauses("axioms intermediate causation");
						}
					}
				}
				

				for (int var : scc.variables){
					assert(task_proxy.get_variables()[var].is_derived());
					assert(axiom_variables[time][var].size() > size_t(layer)+1);
					if (factory->statically_true_derived_predicates.count(var)) continue;
					int eff_var = axiom_variables[time][var][layer+1];
					sat->impliesOr(eff_var,causeVariables[var]);
					assert(causeVariables[var].size());
				}
				registerClauses("axioms causation");
			}
		} else if (scc.isOfImplicationType || scc.isDependentOnOneVariableInternally){
			// implication type or one-dependent SCCs can be evaluated in two steps
			// 1. We need to correctly determine the values coming into the SCC
			
			vector<vector<int>> causeVariablesLayer0 (task_proxy.get_variables().size());
			for (int sccvar : scc.variables){
				assert(factory->statically_true_derived_predicates.count(sccvar) == 0);
				//if (statically_true_derived_predicates.count(sccvar)) {
				//	assertYes(solver,axiom_variables[time][sccvar][0]);
				//	continue;
				//}
				for (OperatorProxy opProxy : factory->achievers_per_derived[sccvar]){

					// Effect
					EffectsProxy effs = opProxy.get_effects();
					assert(effs.size() == 1);
					EffectProxy thisEff = effs[0];
					assert(thisEff.get_fact().get_value() == 1);
					assert(thisEff.get_fact().get_variable().is_derived());
					int eff_var = thisEff.get_fact().get_variable().get_id();
					assert(factory->statically_true_derived_predicates.count(eff_var) == 0);
					int eff_fact_var = get_axiom_var(time,0,thisEff.get_fact());

					set<int> conditions;
					// Preconditions
					PreconditionsProxy precs = opProxy.get_preconditions();
					vector<FactProxy> conds;
				
					for (size_t pre = 0; pre < precs.size(); pre++)
						conds.push_back(precs[pre]);
					
					EffectConditionsProxy cond = thisEff.get_conditions();
					for (size_t i = 0; i < cond.size(); i++)
						conds.push_back(cond[i]);

					// check whether the axiom is internal to the SCC
					bool internalAxiom = false;
					bool inApplicable = false;
					for (FactProxy & fact : conds){
						if (fact.get_variable().is_derived()){
							// the variables that is changed will require value 0
							if (fact.get_variable().get_id() == eff_var){
								assert(fact.get_value() == 0);
								continue;	
							}
							
							if (factory->statically_true_derived_predicates.count(fact.get_variable().get_id())){
								if (fact.get_value()) continue;	// condition is always true
								inApplicable = true;
								break;
							}

							int fact_var;
							if (sset.count(fact.get_variable().get_id())){
								assert(fact.get_value() == 1);
								internalAxiom = true;
								break;
							}

							fact_var = get_last_axiom_var(time,fact);
							conditions.insert(fact_var);
						} else {
							int fact_var = get_fact_var(time,fact);
							conditions.insert(fact_var);
						}
					}

					// internal axioms cannot make facts at layer 0 true.
					if (internalAxiom) continue;
					if (inApplicable) continue;
					
					sat->andImplies(conditions,eff_fact_var);
					registerClauses("axioms evaluation");

					assert(conditions.size() > 0);
					if (conditions.size() == 1){
						causeVariablesLayer0[eff_var].push_back(*conditions.begin());
					} else {
						int intermediateCausation = sat->new_variable();
						variableCounter["axiom causation"]++;
						causeVariablesLayer0[eff_var].push_back(intermediateCausation);
						DEBUG(sat->registerVariable(intermediateCausation,"ca " + utils::pad_int(opProxy.get_id()) + " @ " + utils::pad_int(time) + " " + utils::pad_int(0)));
						for (int required : conditions)
							sat->implies(intermediateCausation, required);
						registerClauses("axioms intermediate causation");
					}
				}
			}

			for (int var : scc.variables){
				if (factory->statically_true_derived_predicates.count(var)) continue;
				if (task_proxy.get_variables()[var].is_derived()){
					int eff_var = axiom_variables[time][var][0];
					sat->impliesOr(eff_var,causeVariablesLayer0[var]);
				}
			}
			registerClauses("axioms causation");


			// 2. Internal work
			if (scc.isOfImplicationType) {
				for (size_t varOffset = 0; varOffset < scc.variables.size(); varOffset++){
					int var = scc.variables[varOffset];
					//if (statically_true_derived_predicates.count(var)) {
					//	assertYes(solver,axiom_variables[time][var][1]);
					//	continue;
					//}
					for (size_t varOffsetTo : scc.directTransitiveImplications[varOffset]){
						int varTo = scc.variables[varOffsetTo];
						sat->implies(axiom_variables[time][var][0], axiom_variables[time][varTo][1]);
					}
				}
				registerClauses("axioms evaluation");

				for (size_t varOffsetTo = 0; varOffsetTo < scc.variables.size(); varOffsetTo++){
					int varTo = scc.variables[varOffsetTo];
					if (factory->statically_true_derived_predicates.count(varTo)) continue;
					vector<int> causeVariables;
					for (size_t varOffset : scc.directTransitiveCauses[varOffsetTo]){
						int var = scc.variables[varOffset];
						causeVariables.push_back(axiom_variables[time][var][0]);
					}
					sat->impliesOr(axiom_variables[time][varTo][1], causeVariables);
				}
				registerClauses("axioms causation");


			} else if (scc.isDependentOnOneVariableInternally){
				VariableProxy varProxy = task_proxy.get_variables()[scc.dependingVariable];
				// iterate over the possible values of the variable and implement the pattern for each one
				for (int value = 0; value < varProxy.get_domain_size(); value++){
					for (size_t varOffset = 0; varOffset < scc.variables.size(); varOffset++){
						int var = scc.variables[varOffset];
						//if (factory->statically_true_derived_predicates.count(var)) {
						//	assertYes(solver,axiom_variables[time][var][1]);
						//	continue;
						//}
						for (size_t varOffsetTo : scc.guardedTransitiveImplications[value][varOffset]){
							int varTo = scc.variables[varOffsetTo];
							// if initial value and dependent one -> final value
							sat->impliesAnd(axiom_variables[time][var][0],
										fact_variables[time][scc.dependingVariable][value],
										axiom_variables[time][varTo][1]);
						}
					}
					registerClauses("axioms evaluation");

					for (size_t varOffsetTo = 0; varOffsetTo < scc.variables.size(); varOffsetTo++){
						int varTo = scc.variables[varOffsetTo];
						if (factory->statically_true_derived_predicates.count(varTo)) continue;
						vector<int> causeVariables;
						for (size_t varOffset : scc.guardedTransitiveCauses[value][varOffsetTo]){
							int var = scc.variables[varOffset];
							causeVariables.push_back(axiom_variables[time][var][0]);
						}
						sat->andImpliesOr(axiom_variables[time][varTo][1],
								fact_variables[time][scc.dependingVariable][value],
								causeVariables);
					}
					registerClauses("axioms causation");

				}
			}	
		} else {
			assert(false); // the SCC must have *some* type
		}
	}
}



void KautzSelmanRintanenEncoding::generate_derived_predicate_variables(int time){
	axiom_variables[time].resize(task_proxy.get_variables().size());
	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
		VariableProxy varProxy = task_proxy.get_variables()[var];
		if (!varProxy.is_derived()) continue;
		// don't need to generate anything for statically true facts.
		if (factory->statically_true_derived_predicates.count(var)) continue;

		axiom_variables[time][var].resize(factory->numberOfAxiomLayerVariablesPerDerived[var] + 1);
		for (int layer = 0; layer <= factory->numberOfAxiomLayerVariablesPerDerived[var]; layer++){
			// variables from axioms must be "boolean"
			assert(varProxy.get_domain_size() == 2);
			
			int factVar = sat->new_variable();
			variableCounter["axioms"]++;
			axiom_variables[time][var][layer] = factVar;
			DEBUG(sat->registerVariable(factVar,"ax " + utils::pad_int(var)+ " @ " + utils::pad_int(time) + " / " + utils::pad_int(layer) + " " + varProxy.get_name() + "=" + varProxy.get_fact(1).get_name()));
			//DEBUG(sat->registerVariable(factVar,"ax " + pad_int(var)+ " @ " + pad_int(time) + " / " + pad_int(layer)));
		}
	}
}

void KautzSelmanRintanenEncoding::encode_direct_preconditions_and_effects_of_action(int time,
		map<FactPair, map<set<int>,vector<int>>> conditionBuffer,
		map<FactPair, map<set<int>, int>> conditionVariable){
	for (size_t op = 0; op < task_proxy.get_operators().size(); op ++){
		DEBUG(if (op && op % 100 == 0){
			log << "Generated Operator " << op << " of " << task_proxy.get_operators().size() << endl;		
		});
		int opvar = operator_variables[time][op];
		OperatorProxy opProxy = task_proxy.get_operators()[op];

		// temporary data structure that contains the preconditions of the action as a map
		// var -> val
		// TODO: the preMap should actually be a global data structure, I created the operator_precondition_map already 
		// TODO: check whether and where we need it depending on conditional effect conditions.
		map<int,int> preMap;
		bool inapplicableOperator = false;

		// Preconditions
		PreconditionsProxy precs = opProxy.get_preconditions();
		for (size_t pre = 0; pre < precs.size(); pre++){
			FactProxy fact = precs[pre];
			preMap[fact.get_variable().get_id()] = fact.get_value();

			// edge case -> if the precondition is a derived predicate that is statically true or false
			// we can optimise it out
			if (factory->statically_true_derived_predicates.count(fact.get_variable().get_id())){
				// this precondition is always true, ignore it.
				if (fact.get_value()) continue;
				// precondition is always false, to disable action
				sat->assertNot(opvar);
				DEBUG(log << "Disabling operator " << opProxy.get_name() << endl);
				inapplicableOperator = true;
				break;
			}

			// get SAT decision variable for this fact
			int fact_var = get_fact_var(time,fact);
			// if operator is selected, its precondition must be true	
			sat->implies(opvar,fact_var);
		}
		registerClauses("preconditions");
		
		if (inapplicableOperator) continue;

		// Effect
		EffectsProxy effs = opProxy.get_effects();
		for (size_t eff = 0; eff < effs.size(); eff++){
			EffectProxy thisEff = effs[eff];
			// SAT variable mentioned by this effect. It is a variable of the next time step!
			int eff_fact_var = get_fact_var(time+1,thisEff.get_fact());


			// case: this could be a conditional effect
			// In FDR actually all effects are conditional. "regular" unconditional effects have a condition of size zero.
			// TODO: could be a vector ...
			set<int> conditions;
			// for edge case: if condition mentions statically true or false derived predicate
			bool inApplicableEffect = false;
			EffectConditionsProxy cond = thisEff.get_conditions();
			// gather the conditions of the conditional effect in a set
			for (size_t i = 0; i < cond.size(); i++){
				// edge case: derived predicates that are statically true or false
				if (factory->statically_true_derived_predicates.count(cond[i].get_variable().get_id())){
					// this precondition is always true, ignore it.
					if (cond[i].get_value()) continue;
					// precondition is always false, to disable action
					inApplicableEffect = true;
					break;
				}
				conditions.insert(get_fact_var(time, cond[i]));
			}

			if (inApplicableEffect) continue;

			// The causation variable is the one that if true forces this effect to happen.
			// These are the variables mentioned in the frame axioms
			// For unconditional effect, this is simply the operator variable itself.
			// For true conditional effects, we need to generate an intermediate variable.
			// The frame axioms then mention this intermediate and the intermediate forces its conditions to be true.
			int thisCausation;

			// unconditional effect
			if (conditions.size() == 0){
				thisCausation = opvar;
			} else {
				// TODO: this might be an optimisation to remove symmetry. Unclear
				// for conditional effects we add that the effect is not yet true
				//conditions.insert(-eff_fact_var_before);

				// operator that has exactly the same condition for the same effect
				if (conditionBuffer[thisEff.get_fact().get_pair()].count(conditions)){
					// reuse the causation variable
					thisCausation = conditionVariable[thisEff.get_fact().get_pair()][conditions];
					conditionBuffer[thisEff.get_fact().get_pair()][conditions].push_back(opvar);
				} else {
					// otherwise, this is a new conditional effect. We need to generate a new causation variable.
					thisCausation = sat->new_variable();
					conditionVariable[thisEff.get_fact().get_pair()][conditions] = thisCausation;
					conditionBuffer[thisEff.get_fact().get_pair()][conditions].push_back(opvar);
					
					variableCounter["facts"]++;
					DEBUG(sat->registerVariable(thisCausation,"ce " + utils::pad_int(op) + " " + utils::pad_int(eff) +" @ " + utils::pad_int(time)));

					// if the causation variable is true, all the conditions of the conditional effect must be true.
					// these clauses are only needed for the frame axioms! The actual checking of the conditions for applying the
					// conditional effect happens later.					
					for (int required : conditions)
						sat->implies(thisCausation,required);
					registerClauses("frame axioms effect causation");
				}
			}

			// encode the actual effects themselves.
			// positive effect
			conditions.insert(opvar);
			// conditions imply the effect.
			// If this is an unconditional effect, conditions only contains the opvar
			// Otherwise it contains opvar and all conditions of the conditional effect
			sat->andImplies(conditions,eff_fact_var);
			// book-keeping for frame axioms: this is a possible cause for the effect fact to become true!
			achieverVars[time][thisEff.get_fact().get_variable().get_id()]
				[thisEff.get_fact().get_value()].push_back(thisCausation);

			// implicit deleting effects, i.e. delete any value of the variable that is set
			for (int val = 0; val < thisEff.get_fact().get_variable().get_domain_size(); val++){
				// the effect var=v implies a deleting effect for all v' != v on var=v'. I.e. they must be false afterwards
				if (val == thisEff.get_fact().get_value()) continue;
				// Maybe an optimisation. If the preconditions already force a different value than v', don't encode it.
				// This test can be removed, but leads to a larger formula. But: the formula might be easier to solve.
				if (preMap.count(thisEff.get_fact().get_variable().get_id()) &&
					preMap[thisEff.get_fact().get_variable().get_id()] != val)
					continue;
	
				// the sat variable for the deleted fact	
				int deletedFact = get_fact_var(time+1, FactPair(thisEff.get_fact().get_variable().get_id(),val));
				// if conditions (for unconditional effect: the opvar) are true, then the deleted fact is false! (thus the -)
				sat->andImplies(conditions,-deletedFact);
			
				// for frame axioms: this is a reason for the fact to turn false!	
				deleterVars[time][thisEff.get_fact().get_variable().get_id()]
					[val].push_back(thisCausation);
			}
			registerClauses("effects");
		}
	}
}








void KautzSelmanRintanenEncoding::encode(int fromTime, int toTime) {
	curClauseNumber = sat->get_number_of_clauses();
	//////////////////////////////////////////////////////////////////
	////////////// 1. Generate all base variables (actions and facts)

	// variables representing operators
	if (operator_variables.count(fromTime) == 0) generate_operator_variables(fromTime);
	DEBUG(log << "New Operator Variables Generated " << sat->number_of_variables<< endl);
	
	// variables representing facts
	if (fact_variables.count(fromTime) == 0) generate_fact_variables(fromTime);
	if (fact_variables.count(toTime) == 0) generate_fact_variables(toTime);
	DEBUG(log << "New State Variables Generated " << sat->number_of_variables<< endl);

	// variables for the truth of derived predicates.	
	if (axiom_variables.count(fromTime) == 0) generate_derived_predicate_variables(fromTime);
	if (axiom_variables.count(toTime) == 0) generate_derived_predicate_variables(toTime);
	DEBUG(log << "New Axiom Variables generated " << sat->number_of_variables << endl);

	//////////////////////////////////////////////////////////////////
	// 2. Data structures for frame axioms.
	// We keep for every timestep t -> for every FDR variable var -> for every value v of that variable -> a list
	// of SAT variables that if true explain why var=v at time t. In the simplest case, these are operator variables of actions that hae
	// var=v as an effect.
	achieverVars[fromTime].resize(task_proxy.get_variables().size());
	deleterVars[fromTime].resize(task_proxy.get_variables().size());
	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
		VariableProxy varProxy = task_proxy.get_variables()[var];
		// derived predicates don't have frame axioms
		if (varProxy.is_derived()) continue;				
		achieverVars[fromTime][var].resize(varProxy.get_domain_size());
		deleterVars[fromTime][var].resize(varProxy.get_domain_size());
	}

	//////////////////////////////////////////////////////////////////
	// 3. action effects / preconditions
	DEBUG(log << "=> Generating timestep " << fromTime << endl);

	// if two actions have the same condition effect, we don't need to encode that effect twice.
	// The conditional buffer gives: Fact of effect -> set of condition variables AS SAT VARIABLES -> operator variables with that effect.
	// the conditionVariable is the variable expressing that this conditional effect is active.
	// Note: the buffers are *per timestep*
	map<FactPair, map<set<int>,vector<int>>> conditionBuffer;
	map<FactPair, map<set<int>, int>> conditionVariable;

	encode_direct_preconditions_and_effects_of_action(fromTime, conditionBuffer, conditionVariable);

	// selectable operators for frame axioms
	for (auto [fp, m] : conditionBuffer){
		for (auto [con, opVars] : m){
			int conditionVar = conditionVariable[fp][con];
			// the conditionVar represents the fact that this condition should turn true.
			// If so, one of the causes of that condition must be true
			// Note: this is only needed to support conditional effects
			sat->impliesOr(conditionVar,opVars);
		}
	}
	registerClauses("frame axioms effect causation");

	//////////////////////////////////////////////////////////////////
	// 4. Frame axioms
	for (size_t var = 0; var < task_proxy.get_variables().size(); var++){
		VariableProxy varProxy = task_proxy.get_variables()[var];
		// derived predicates don't have frame axioms
		if (varProxy.is_derived()) continue;				
		for (int val = 0; val < varProxy.get_domain_size(); val++){
			int thisTime = fact_variables[fromTime][var][val];
			int nextTime = fact_variables[toTime][var][val];

			// two flanks for frame axioms:
			// if variable true nextTime, but false thisTime, one of the achievers must be true
			sat->impliesPosAndNegImpliesOr(nextTime,thisTime,achieverVars[fromTime][var][val]);
			// if variable false nextTime, but true thisTime, one of the deleters must be true
			// TODO: in theory this can be removed safely - as each var has exactly one value and
			// there are only positive preconditions. Check and experiment.
			sat->impliesPosAndNegImpliesOr(thisTime,nextTime,deleterVars[fromTime][var][val]);
		}
	}
	DEBUG(log << "Frame axioms." << endl);
	registerClauses("frame axioms");

	//////////////////////////////////////////////////////////////////
	// 5. Evaluation of axioms
	// assumption here is: the variables in fact_variables are the ones
	// that are supposed to be used for preconditions
	//AxiomsProxy axioms = task_proxy.get_axioms();
	DEBUG(log << "=> Generating axioms for timestep " << fromTime  << endl);
	axiom_encoding_for_timestep(fromTime);
	DEBUG(log << "Axioms done." << endl);

	//////////////////////////////////////////////////////////////////
	// 6. Mutexes
	
	
	//////////////////////////////////////////////////////////////////
	// 9. Action Control -- Determine which actions can be executed in parallel.
	if (existsStep)
		// exists-step gets all operator variables -- otherwise indexing is too difficult
		exists_step_restriction(operator_variables[fromTime], fromTime);
	else
		sat->atMostOne(operator_variables[fromTime]);

	if (forceAtLeastOneAction) sat->atLeastOne(operator_variables[fromTime]);

	registerClauses("action control");
}

int KautzSelmanRintanenEncoding::get_derived_goal_var(int time, FactProxy goal){
	return get_last_axiom_var(time, goal);
}

}
