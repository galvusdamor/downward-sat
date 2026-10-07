#include "disabling_graph_ordering.h"
#include "file_ordering.h"
#include "input_ordering.h"
#include "random_ordering.h"
#include "topological_ordering.h"

#include "../../plugins/plugin.h"
#include "../../utils/rng_options.h"

using namespace std;

namespace plugin_sat {

static class ActionOrderingCategoryPlugin : public plugins::TypedCategoryPlugin<sat_search::ActionOrdering> {
public:
	ActionOrderingCategoryPlugin() : TypedCategoryPlugin("ActionOrdering") {
		document_synopsis(
			"Action orderings (rankings) used by the exists-step style SAT encodings. "
			"Actions inside a parallel step are executed according to this ordering.");
	}
}
_category_plugin;

class InputOrderingFeature : public plugins::TypedFeature<sat_search::ActionOrdering, sat_search::InputOrdering> {
public:
	InputOrderingFeature() : TypedFeature("input") {
		document_title("Input ordering");
		document_synopsis("Order operators as in the input (operator IDs ascending).");
		sat_search::add_action_ordering_options_to_feature(*this);
	}

	virtual shared_ptr<sat_search::InputOrdering>
	create_component(const plugins::Options &opts) const override {
		return plugins::make_shared_from_arg_tuples<sat_search::InputOrdering>(
			sat_search::get_action_ordering_arguments_from_options(opts));
	}
};
static plugins::FeaturePlugin<InputOrderingFeature> _plugin_input;

class RandomOrderingFeature : public plugins::TypedFeature<sat_search::ActionOrdering, sat_search::RandomOrdering> {
public:
	RandomOrderingFeature() : TypedFeature("random") {
		document_title("Random ordering");
		document_synopsis("Order operators randomly.");
		utils::add_rng_options_to_feature(*this);
		sat_search::add_action_ordering_options_to_feature(*this);
	}

	virtual shared_ptr<sat_search::RandomOrdering>
	create_component(const plugins::Options &opts) const override {
		return plugins::make_shared_from_arg_tuples<sat_search::RandomOrdering>(
			utils::get_rng_arguments_from_options(opts),
			sat_search::get_action_ordering_arguments_from_options(opts));
	}
};
static plugins::FeaturePlugin<RandomOrderingFeature> _plugin_random;

class TopologicalOrderingFeature : public plugins::TypedFeature<sat_search::ActionOrdering, sat_search::TopologicalOrdering> {
public:
	TopologicalOrderingFeature() : TypedFeature("tsort") {
		document_title("Topological ordering");
		document_synopsis("Balyo's Topological Ranking: topological order of the enabling graph, ignoring cycles.");
		sat_search::add_action_ordering_options_to_feature(*this);
	}

	virtual shared_ptr<sat_search::TopologicalOrdering>
	create_component(const plugins::Options &opts) const override {
		return plugins::make_shared_from_arg_tuples<sat_search::TopologicalOrdering>(
			sat_search::get_action_ordering_arguments_from_options(opts));
	}
};
static plugins::FeaturePlugin<TopologicalOrderingFeature> _plugin_tsort;

class FileOrderingFeature : public plugins::TypedFeature<sat_search::ActionOrdering, sat_search::FileOrdering> {
public:
	FileOrderingFeature() : TypedFeature("from_file") {
		document_title("Ordering from file");
		document_synopsis("Read the ordering from a file containing whitespace separated operator IDs in execution order. "
			"Operator IDs refer to the SAS file read by the search component.");
		add_option<string>("filename", "file containing the ordering");
		sat_search::add_action_ordering_options_to_feature(*this);
	}

	virtual shared_ptr<sat_search::FileOrdering>
	create_component(const plugins::Options &opts) const override {
		return plugins::make_shared_from_arg_tuples<sat_search::FileOrdering>(
			opts.get<string>("filename"),
			sat_search::get_action_ordering_arguments_from_options(opts));
	}
};
static plugins::FeaturePlugin<FileOrderingFeature> _plugin_file;

class DisablingGraphOrderingFeature : public plugins::TypedFeature<sat_search::ActionOrdering, sat_search::DisablingGraphOrdering> {
public:
	DisablingGraphOrderingFeature() : TypedFeature("disabling") {
		document_title("Disabling graph ordering");
		document_synopsis("Rintanen's ordering based on the SCCs of the disabling graph.");
   		add_option<int>(
            "threshold",
            "threshold for the size of the disabling graph. If graph becomes larger than this, make simplifying assumptions.",
            "5000000");
   		add_option<bool>(
            "join_groups_above_threshold",
            "join all groups of operators above the exists-step threshold into a single big group",
            "true");
		sat_search::add_action_ordering_options_to_feature(*this);
	}

	virtual shared_ptr<sat_search::DisablingGraphOrdering>
	create_component(const plugins::Options &opts) const override {
		return plugins::make_shared_from_arg_tuples<sat_search::DisablingGraphOrdering>(
			opts.get<int>("threshold"),
			opts.get<bool>("join_groups_above_threshold"),
			sat_search::get_action_ordering_arguments_from_options(opts));
	}
};
static plugins::FeaturePlugin<DisablingGraphOrderingFeature> _plugin_disabling;
}
