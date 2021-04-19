#ifndef __XMLOUTPUTGENERATOR
#define __XMLOUTPUTGENERATOR

#include <vector>
#include <map>
#include <string>
#include <variant>
#include <stack>
#include <queue>
#include <set>
#include <memory>

#include <boost/graph/adjacency_list.hpp>

#include "outputgenerator.hpp"

class XMLOutputGenerator : public FileOutputGenerator {
    public:
        void generate_instances_output(ATGraph mission_decomposition, GMGraph gm, std::pair<std::string,std::string> output, std::vector<ground_literal> world_state, std::vector<SemanticMapping> semantic_mapping,
                                std::map<std::string,set<std::string>> sorts, std::vector<sort_definition> sort_definitions, std::vector<predicate_definition> predicate_definitions);
        void output_actions(pt::ptree& output_file, std::map<std::string,task> actions);
        void output_constraints(pt::ptree& output_file, std::vector<Constraint> final_mission_constraints, std::map<std::string,std::string> task_id_map);
        void output_mission_decompositions(pt::ptree& output_file, std::vector<std::vector<std::pair<int,ATNode>>> valid_mission_decompositions, std::map<std::string,std::string> task_id_map);

        std::map<std::string,std::string> output_tasks(pt::ptree& output_file, std::vector<Decomposition> task_instances, std::vector<SemanticMapping> semantic_mapping);
};

#endif