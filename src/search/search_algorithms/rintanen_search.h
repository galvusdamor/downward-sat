#ifndef SEARCH_ALGORITHMS_RINTANEN_SAT_SEARCH
#define SEARCH_ALGORITHMS_RINTANEN_SAT_SEARCH

#include "../search_algorithm.h"
#include "../sat/sat_encoder.h"
#include "../sat/sat_encoding.h"


struct SAT_Scheduler;

namespace sat_search{

class LengthStrategy;

class RintanenSATSearch : public SearchAlgorithm, std::enable_shared_from_this<RintanenSATSearch>{
	const std::shared_ptr<LengthStrategy> length_strategy;
	const std::shared_ptr<SATEncodingFactory> encoding_factory;
	const size_t max_parallel_calls;
	const int scheduler_interval_seconds;
	const bool dont_schedule_formula_generation;
	const int memory_limit_mbs;

protected:
    virtual void initialize() override;
	virtual SearchStatus step() override;

public:
    explicit RintanenSATSearch(
		int _max_parallel_calls,
		int _scheduler_interval_seconds,
		bool _dont_schedule_formula_generation,
		int _memory_limit_mbs,
		std::shared_ptr<LengthStrategy> _length_strategy, std::shared_ptr<SATEncodingFactory> _encoding_factory,
		bool _kissat_quietMode,OperatorCost cost_type, int bound, double max_time, const std::string &description,  utils::Verbosity verbosity);
    virtual ~RintanenSATSearch() = default;

    // needs to be public as accessed by thread
	using SearchAlgorithm::set_plan;
	
	// returns true if there is a next run that could be generated
	bool create_next_length_run(std::shared_ptr<SAT_Scheduler> global_scheduler);
    
	virtual void print_statistics() const override;
};
}

#endif
