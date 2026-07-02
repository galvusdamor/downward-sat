#include "incremental_sat_search.h"
#include "search_common.h"

#include "../plugins/plugin.h"

//inc_strategy(),
using namespace std;

namespace plugin_sat {

static plugins::TypedEnumPlugin<sat_search::incremental_strategy> _enum_plugin({
        {"goal", "add new time-steps after the current goal"},
        {"init", "add new time-steps before the current goal"},
        {"middle", "add new time-steps in the middle"}
    });
	
	
class IncrementalSATSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, sat_search::IncrementalSATSearch> {
public:
    IncrementalSATSearchFeature() : TypedFeature("inc") {
    	document_title("Incremental SAT Search");
		document_synopsis("");

		add_option<shared_ptr<sat_search::LengthStrategy>> (
			"length_strategy","strategy to determine plan lengths", "one_by_one()");
		add_option<shared_ptr<sat_search::SATEncodingFactory>> (
			"encoder","type of formula to use for encoding", "ksr()");

		add_option<bool>(
			"solver_quiet",
			"if possible try to put the SAT solver into quiet mode (less output to parse for experiments)",
			"false");

		add_option<sat_search::incremental_strategy>("strategy",
		                       "incremental strategy",
		                       "GOAL");

		add_search_algorithm_options_to_feature(*this, "inc");

	}

    virtual shared_ptr<sat_search::IncrementalSATSearch>
    create_component(const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<sat_search::IncrementalSATSearch>(
			opts.get<shared_ptr<sat_search::LengthStrategy>>("length_strategy"),
			opts.get<shared_ptr<sat_search::SATEncodingFactory>>("encoder"),
			opts.get<sat_search::incremental_strategy>("strategy"),
			opts.get<bool>("solver_quiet"),
            get_search_algorithm_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<IncrementalSATSearchFeature> _plugin;
}

