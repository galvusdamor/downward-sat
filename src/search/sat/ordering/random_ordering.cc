#include "random_ordering.h"

#include "../../utils/rng.h"
#include "../../utils/rng_options.h"

using namespace std;

namespace sat_search {

ActionOrderingResult RandomOrdering::compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &, utils::LogProxy &){
	vector<int> order;
	for (size_t op = 0; op < task_proxy.get_operators().size(); op++)
		order.push_back(op);
	utils::get_rng(random_seed)->shuffle(order);
	return make_single_group_result(move(order));
}
}
