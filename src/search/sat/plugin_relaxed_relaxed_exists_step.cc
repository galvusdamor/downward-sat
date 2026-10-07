#include "relaxed_relaxed_exists_step.h"
#include "../plugins/plugin.h"

using namespace std;

namespace plugin_sat {
class RelaxedRelaxedExistsStepEncodingFeature : public plugins::TypedFeature<sat_search::SATEncodingFactory, sat_search::RelaxedRelaxedExistsStepEncodingFactory> {
public:
    RelaxedRelaxedExistsStepEncodingFeature() : TypedFeature("r2e") {
        document_title("Relaxed Relaxed Exists-Step encoding");
        document_synopsis("Encoding by Balyo (ICTAI 2013). Does not support conditional effects and axioms.");

   		add_option<bool>(
            "prune_opponents",
            "an operator with precondition var=u and an effect on var is only considered to destroy var=u",
            "true");
		sat_search::add_base_encoding_options_to_feature(*this, "tsort()");
    }

    virtual shared_ptr<sat_search::RelaxedRelaxedExistsStepEncodingFactory>
    create_component(const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<sat_search::RelaxedRelaxedExistsStepEncodingFactory>(
			opts.get<bool>("prune_opponents"),
			sat_search::get_base_encoding_arguments_from_options(opts)
			);
    }
};

static plugins::FeaturePlugin<RelaxedRelaxedExistsStepEncodingFeature> _plugin;
}
