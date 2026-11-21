#ifndef SAT_EXISTS_STEP_SIGMA_H
#define SAT_EXISTS_STEP_SIGMA_H

#include "base_encoding.h"

#include <memory>
#include <vector>

namespace sat_search {

// Exists-step encoding with a fixed action ranking that tracks the actual intermediate states inside a time step,
// following the sigma_i(v) construction of the symbolic pattern planning (SPP) encoding by Cardellini, Giunchiglia
// and Maratea (AIJ 2026, Section 3.2), restricted to classical planning (Boolean action variables, no rolling).
//
// For every fact f and every operator a_i (in rank order) that changes f, a new SAT variable
// sigma_i(f) is introduced with
//   sigma_i(x=v) <-> sigma_{i-1}(x=v) v  a_i     if x=v  in eff(a_i)
//   sigma_i(x=v) <-> sigma_{i-1}(x=v) ^ -a_i     if x=v' in eff(a_i), v' != v
// sigma_0(f) is the fact variable at the start of the step, the last sigma of f is the fact variable at the end
// of the step. Preconditions are checked against sigma_{i-1}: a_i -> sigma_{i-1}(pre).
// A step can contain any set of actions that is executable in rank order (as in R2E with the same ranking), but
// instead of R2E's interference chains the intermediate states are represented explicitly.
// Tasks with conditional effects or axioms are not supported.
class ExistsStepSigmaEncodingFactory : public BaseEncodingFactory {
	const bool prune_deletes;

public:
	// a change of a fact's truth value caused by an operator
	struct Change {
		FactPair fact;
		bool becomes_true;
		// last change of this fact in the ranking; its variable is the fact variable at the end of the step
		bool is_last;
	};

	// operator -> its preconditions
	std::vector<std::vector<FactPair>> preconditions;
	// operator -> the facts it changes
	std::vector<std::vector<Change>> changes;
	// facts that are not changed by any operator (frame: unchanged over a step)
	std::vector<FactPair> unchanged_facts;

protected:
	void analyse_task() override;
	void compute_action_ordering() override;
	void prepare_encoding() override;

public:
	ExistsStepSigmaEncodingFactory(
			bool _prune_deletes,
			std::shared_ptr<ActionOrdering> _ordering,
			const std::string &_write_ordering_file,
			bool _stop_after_ordering,
			bool _encode_fdr_mutexes,
			bool _encode_additional_mutexes,
			bool forceAtLeastOneAction) :
		BaseEncodingFactory(_ordering, _write_ordering_file, _stop_after_ordering,
			_encode_fdr_mutexes, _encode_additional_mutexes, forceAtLeastOneAction),
		prune_deletes(_prune_deletes) {}

	virtual std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
};

class ExistsStepSigmaEncoding : public BaseEncoding {
	ExistsStepSigmaEncodingFactory* factory;
public:
	ExistsStepSigmaEncoding(
		ExistsStepSigmaEncodingFactory* _factory,
		std::shared_ptr<sat_capsule> capsule, const TaskProxy _task_proxy, bool forceAtLeastOneAction,
		utils::LogProxy _log);

	void encode(int fromTime, int toTime) override;
};
}

#endif
