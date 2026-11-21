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
		// 1-based position of the current entry, for error messages
		size_t position = order.size() + 1;
		if (parsed != token.size()){
			cerr << "Error: entry " << position << " of action ordering file " << filename << " is '" << token
				<< "', which is not an operator ID (expected an integer between 0 and " << number_of_operators - 1 << ")." << endl;
			utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
		}
		if (op < 0 || op >= number_of_operators){
			cerr << "Error: entry " << position << " of action ordering file " << filename << " is " << op
				<< ", which is not a valid operator ID. The task has " << number_of_operators
				<< " operators, so IDs must be between 0 and " << number_of_operators - 1 << "." << endl;
			utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
		}
		if (seen[op]){
			cerr << "Error: operator ID " << op << " appears more than once in action ordering file " << filename
				<< " (again at entry " << position << "). Each operator must be listed exactly once." << endl;
			utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
		}
		seen[op] = true;
		order.push_back(op);
	}
	if (static_cast<int>(order.size()) != number_of_operators){
		// all entries are distinct and valid, so some operators are missing
		cerr << "Error: action ordering file " << filename << " lists " << order.size() << " of the task's "
			<< number_of_operators << " operators. Each operator must be listed exactly once. Missing operator IDs:";
		const int max_reported = 10;
		int reported = 0;
		for (int op = 0; op < number_of_operators && reported < max_reported; op++){
			if (seen[op]) continue;
			cerr << " " << op;
			reported++;
		}
		int missing = number_of_operators - static_cast<int>(order.size());
		if (missing > max_reported)
			cerr << " ... (" << missing << " in total)";
		cerr << endl;
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
