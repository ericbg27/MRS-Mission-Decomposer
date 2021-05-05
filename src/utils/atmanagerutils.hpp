#ifndef __AT_MANAGER_UTILS
#define __AT_MANAGER_UTILS

#include <string>
#include <vector>
#include <map>
#include <variant>

#include <boost/property_tree/ptree.hpp>

#include "at.hpp"
#include "parsetree.hpp"
#include "../config/config.hpp"
#include "../knowledgemanager/knowledgemanager.hpp"
#include "../gm/gm.hpp"

namespace pt = boost::property_tree;

void print_at_instances_info(std::map<std::string,std::vector<AbstractTask>> at_instances);
void print_at_paths_info(std::map<std::string,std::vector<std::vector<task>>> at_decomposition_paths);

bool check_path_validity(std::vector<task> path, std::vector<ground_literal> world_state, AbstractTask at, std::vector<SemanticMapping> semantic_mappings);

void solve_query_statement(pt::ptree queried_tree, QueriedProperty q, GMGraph gm, int node_id, std::map<std::string,std::pair<std::string,std::vector<pt::ptree>>>& valid_variables, 
							std::map<std::string, std::variant<std::pair<std::string,std::string>,std::pair<std::vector<std::string>,std::string>>>& gm_var_map);

pt::ptree get_query_ptree(GMGraph gm, int node_id, std::map<std::string,std::pair<std::string,std::vector<pt::ptree>>> valid_variables, std::map<int,AchieveCondition> valid_forAll_conditions, std::map<std::string,std::pair<int,QueriedProperty>>& props_to_query,
                            std::map<std::string, std::variant<pair<std::string,std::string>,pair<std::vector<std::string>,std::string>>> gm_var_map, pt::ptree world_tree);

#endif