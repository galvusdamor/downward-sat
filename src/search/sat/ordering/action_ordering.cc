#include "action_ordering.h"

#include "../../plugins/plugin.h"
#include "../../utils/system.h"

#include <algorithm>
#include <fstream>

using namespace std;

namespace sat_search {

ActionOrderingResult ActionOrdering::compute(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log){
	ActionOrderingResult result = compute_ordering(task_proxy, interference, log);
	assert(result.order.size() == task_proxy.get_operators().size());
	if (inverted){
		// the groups were only valid for the original direction, so fall back to a single group
		reverse(result.order.begin(), result.order.end());
		result.groups.clear();
		result.groups.push_back(result.order);
	}
	log << "Computed action ordering " << get_name() << (inverted ? " (inverted)" : "")
		<< " with " << result.groups.size() << " group(s)." << endl;
	return result;
}

ActionOrderingResult make_single_group_result(vector<int> order){
	ActionOrderingResult result;
	result.groups.push_back(order);
	result.order = move(order);
	return result;
}

void write_ordering_to_file(const vector<int> &order, const string &filename){
	ofstream out(filename);
	if (!out){
		cerr << "Error: could not open file " << filename << " for writing the action ordering." << endl;
		utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
	}
	for (size_t i = 0; i < order.size(); i++){
		if (i) out << " ";
		out << order[i];
	}
	out << endl;
}

vector<int> read_ordering_from_file(const string &filename, int number_of_operators){
	ifstream in(filename);
	if (!in){
		cerr << "Error: could not open action ordering file " << filename << endl;
		utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
	}
	vector<int> order;
	vector<bool> seen(number_of_operators, false);
	string token;
	while (in >> token){
		size_t parsed = 0;
		int op = -1;
		try {
			op = stoi(token, &parsed);
		} catch (const exception &){
			parsed = 0;
		}
		if (parsed != token.size()){
			cerr << "Error: action ordering file " << filename << " contains invalid token '" << token << "'" << endl;
			utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
		}
		if (op < 0 || op >= number_of_operators){
			cerr << "Error: action ordering file " << filename << " contains operator ID " << op
				<< ", but the task has only " << number_of_operators << " operators" << endl;
			utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
		}
		if (seen[op]){
			cerr << "Error: action ordering file " << filename << " contains operator ID " << op << " twice" << endl;
			utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
		}
		seen[op] = true;
		order.push_back(op);
	}
	if (static_cast<int>(order.size()) != number_of_operators){
		cerr << "Error: action ordering file " << filename << " contains " << order.size()
			<< " operator IDs, but the task has " << number_of_operators << " operators" << endl;
		utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
	}
	return order;
}

void add_action_ordering_options_to_feature(plugins::Feature &feature){
	feature.add_option<bool>(
		"inverted",
		"invert the computed ordering (the last operator is executed first)",
		"false");
}

tuple<bool> get_action_ordering_arguments_from_options(const plugins::Options &opts){
	return make_tuple(opts.get<bool>("inverted"));
}

}
