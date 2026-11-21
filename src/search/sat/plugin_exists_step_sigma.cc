#include "exists_step_sigma.h"
#include "../plugins/plugin.h"

using namespace std;

namespace plugin_sat {
class ExistsStepSigmaEncodingFeature : public plugins::TypedFeature<sat_search::SATEncodingFactory, sat_search::ExistsStepSigmaEncodingFactory> {
public:
    ExistsStepSigmaEncodingFeature() : TypedFeature("sigma") {
        document_title("Exists-step encoding with explicit intermediate states");
        document_synopsis("Exists-step encoding for a fixed action ranking that tracks the intermediate states inside "
            "a step (the sigma construction of the symbolic pattern planning encoding by Cardellini, Giunchiglia and "
            "Maratea, AIJ 2026) instead of using R2E interference chains. Does not support conditional effects and axioms.");

        add_option<bool>(
            "prune_deletes",
            "an operator with precondition var=u and an effect on var only changes var=u and the effect value",
            "true");
        sat_search::add_base_encoding_options_to_feature(*this, "tsort()");
    }

    virtual shared_ptr<sat_search::ExistsStepSigmaEncodingFactory>
    create_component(const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<sat_search::ExistsStepSigmaEncodingFactory>(
            opts.get<bool>("prune_deletes"),
            sat_search::get_base_encoding_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<ExistsStepSigmaEncodingFeature> _plugin;
}
