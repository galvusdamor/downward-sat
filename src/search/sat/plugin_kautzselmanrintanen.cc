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
		sat_search::add_base_encoding_options_to_feature(*this, "disabling()");
    }


    virtual shared_ptr<sat_search::KautzSelmanRintanenEncodingFactory>
    create_component(const plugins::Options &opts) const override {
        plugins::Options options_copy(opts);
        return plugins::make_shared_from_arg_tuples<sat_search::KautzSelmanRintanenEncodingFactory>(
			options_copy.get<int>("encoding"),
			sat_search::get_base_encoding_arguments_from_options(options_copy)
			);
    }


};

static plugins::FeaturePlugin<KautzSelmanRintanenEncodingFeature> _plugin;
}
