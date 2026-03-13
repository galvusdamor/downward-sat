#include "kautz_selman_rintanen.h"
#include "../plugins/plugin.h"

using namespace std;

namespace plugin_sat {
class KautzSelmanRintanenEncodingFeature : public plugins::TypedFeature<sat_search::SATEncodingFactory, sat_search::KautzSelmanRintanenEncodingFactory> {
public:
    KautzSelmanRintanenEncodingFeature() : TypedFeature("ksr") {
        document_title("Encoding by Kautz Selman and Rintanen");
        document_synopsis("");
		
		add_option<int>(
            "encoding",
            "set the encoding. Currently supported are OneStep: 0 and ExistsStep: 2",
            "2");
   		add_option<int>(
            "disabling_threshold",
            "threshold for the size of the disabling graph. If graph becomes larger than this, make simplifying assumptions.",
            "5000000");
   		add_option<bool>(
            "join_groups_above_threshold",
            "join all groups of operators above the exists-step threshold into a single big group",
            "true");
   		add_option<bool>(
            "forceAtLeastOneAction",
            "force at least one action per timestep",
            "true");
    }


    virtual shared_ptr<sat_search::KautzSelmanRintanenEncodingFactory>
    create_component(const plugins::Options &opts) const override {
        plugins::Options options_copy(opts);
        return plugins::make_shared_from_arg_tuples<sat_search::KautzSelmanRintanenEncodingFactory>(
			options_copy.get<int>("encoding"),
			options_copy.get<int>("disabling_threshold"),
			options_copy.get<bool>("join_groups_above_threshold"),
			options_copy.get<bool>("forceAtLeastOneAction")
			);
    }


};

static plugins::FeaturePlugin<KautzSelmanRintanenEncodingFeature> _plugin;
}
