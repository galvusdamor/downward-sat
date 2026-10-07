#include "input_ordering.h"

using namespace std;

namespace sat_search {

ActionOrderingResult InputOrdering::compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &, utils::LogProxy &){
	vector<int> order;
	for (size_t op = 0; op < task_proxy.get_operators().size(); op++)
		order.push_back(op);
	return make_single_group_result(move(order));
}
}
