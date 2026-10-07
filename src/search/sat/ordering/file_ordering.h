#ifndef SAT_ORDERING_FILE_ORDERING_H
#define SAT_ORDERING_FILE_ORDERING_H

#include "action_ordering.h"

namespace sat_search {

// Reads the ordering from a file containing whitespace separated operator IDs in execution order.
class FileOrdering : public ActionOrdering {
	const std::string filename;
protected:
	ActionOrderingResult compute_ordering(const TaskProxy &task_proxy,
		const InterferenceInfo &interference, utils::LogProxy &log) override;
public:
	FileOrdering(const std::string &_filename, bool inverted) : ActionOrdering(inverted), filename(_filename) {}
	std::string get_name() const override {return "from_file(" + filename + ")";}
};
}

#endif
