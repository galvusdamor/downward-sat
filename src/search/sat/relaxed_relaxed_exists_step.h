#ifndef SAT_RELAXED_RELAXED_EXISTS_STEP_H
#define SAT_RELAXED_RELAXED_EXISTS_STEP_H

#include "base_encoding.h"

#include <memory>
#include <vector>

namespace sat_search {

// The Relaxed Relaxed Exists-Step (R^2 E) encoding by Balyo (ICTAI 2013; PhD thesis 2014, Section 3.4).
// A time step may contain any set of actions that is executable in the order given by the action ranking.
// Neither preconditions need to hold at the beginning of the step nor do all effects need to hold at its end.
// Tasks with conditional effects or axioms are not supported.
class RelaxedRelaxedExistsStepEncodingFactory : public BaseEncodingFactory {
	const bool prune_opponents;

public:
	// position of each operator in global_action_ordering
	std::vector<int> rank;

	// var -> value -> operators that have var=value as an effect, sorted by rank
	std::vector<std::vector<std::vector<int>>> supporters;
	// var -> operators that have var in their scope (effects and prevail conditions), sorted by rank
	std::vector<std::vector<int>> scope_operators;
	// operator -> its preconditions
	std::vector<std::vector<FactPair>> preconditions;
	// operator -> its effects and prevail conditions
	std::vector<std::vector<FactPair>> effprev;

	// interference chain (thesis, equation 3.23) of one fact
	struct ChainElement {
		int op;
		bool is_requirer;
		bool is_opponent;
		bool is_supporter;
	};
	// only contains the part of the chain from the first opponent to the last requirer; chains without
	// any opponent before a requirer are omitted
	std::vector<std::pair<FactPair,std::vector<ChainElement>>> chains;

protected:
	void analyse_task() override;
	void compute_action_ordering() override;
	void prepare_encoding() override;

public:
	RelaxedRelaxedExistsStepEncodingFactory(
			bool _prune_opponents,
			std::shared_ptr<ActionOrdering> _ordering,
			const std::string &_write_ordering_file,
			bool _stop_after_ordering,
			bool _encode_fdr_mutexes,
			bool _encode_additional_mutexes,
			bool forceAtLeastOneAction) :
		BaseEncodingFactory(_ordering, _write_ordering_file, _stop_after_ordering,
			_encode_fdr_mutexes, _encode_additional_mutexes, forceAtLeastOneAction),
		prune_opponents(_prune_opponents) {}

	virtual std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
};

class RelaxedRelaxedExistsStepEncoding : public BaseEncoding {
	RelaxedRelaxedExistsStepEncodingFactory* factory;

	void encode_preconditions(int time);
	void encode_effects(int fromTime, int toTime);
	void encode_frame_axioms(int fromTime, int toTime);
	void encode_interference_chains(int time);
public:
	RelaxedRelaxedExistsStepEncoding(
		RelaxedRelaxedExistsStepEncodingFactory* _factory,
		std::shared_ptr<sat_capsule> capsule, const TaskProxy _task_proxy, bool forceAtLeastOneAction,
		utils::LogProxy _log);

	void encode(int fromTime, int toTime) override;
};
}

#endif
