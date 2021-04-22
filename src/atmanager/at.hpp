#ifndef __AT
#define __AT

#include <vector>
#include <string>
#include <map>
#include <variant>

#include "../utils/domain.hpp"

struct AbstractTask  {
    std::string id;
    std::string name;
    task at;
    bool fixed_robot_num;
    std::variant<int,std::pair<int,int>> robot_num;
    std::pair<std::variant<std::vector<std::string>,std::string>,std::pair<std::string,std::string>> location;
    std::vector<std::pair<std::pair<std::variant<std::vector<std::string>,std::string>,std::string>,std::string>> variable_mapping;
    std::vector<std::string> triggering_events;
    std::vector<std::string> params;
};

#endif