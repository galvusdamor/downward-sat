#include "file_ordering.h"

using namespace std;

namespace sat_search {

ActionOrderingResult FileOrdering::compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &, utils::LogProxy &log){
	log << "Reading action ordering from " << filename << endl;
	return make_single_group_result(read_ordering_from_file(filename, task_proxy.get_operators().size()));
}
}
