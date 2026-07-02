#include "rintanen_search.h"
#include "search_common.h"

#include "../plugins/plugin.h"

using namespace std;

namespace plugin_sat {
class RintanenSATSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, sat_search::RintanenSATSearch> {
public:
    RintanenSATSearchFeature() : TypedFeature("schedule") {
        document_title("Rintanen's Pseudo-Parallel SAT Search via scheduling");
        document_synopsis("");

		add_option<shared_ptr<sat_search::LengthStrategy>> (
			"length_strategy","strategy to determine plan lengths", "by_iteration()");
		add_option<shared_ptr<sat_search::SATEncodingFactory>> (
			"encoder","type of formula to use for encoding", "ksr()");

		add_option<int> (
			"max_parallel_calls","maximum number of SAT calls run in parallel", "20");
		add_option<int> (
			"scheduler_interval","interval for the schedule in seconds", "1");
		add_option<int> (
			"memory_limit_mb","memory limit in MBs", "3500");
		add_option<bool> (
			"schedule_formula_as_one","schedule the creation of a formula as one item (helps keeping to the memory limit)", "true");

		add_option<bool>(
			"solver_quiet",
			"if possible try to put the SAT solver into quiet mode (less output to parse for experiments)",
			"false");
    	
		add_search_algorithm_options_to_feature(*this, "schedule");
	}

    virtual shared_ptr<sat_search::RintanenSATSearch>
    create_component(const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<sat_search::RintanenSATSearch>(
			opts.get<int>("max_parallel_calls"),
			opts.get<int>("scheduler_interval"),
			opts.get<bool>("schedule_formula_as_one"),
			opts.get<int>("memory_limit_mb"),
			opts.get<shared_ptr<sat_search::LengthStrategy>>("length_strategy"),
			opts.get<shared_ptr<sat_search::SATEncodingFactory>>("encoder"),
			opts.get<bool>("solver_quiet"),
            get_search_algorithm_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<RintanenSATSearchFeature> _plugin;
}

