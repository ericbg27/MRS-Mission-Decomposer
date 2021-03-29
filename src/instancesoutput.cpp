#include "instancesoutput.hpp"

#include <iostream>

#include <boost/property_tree/xml_parser.hpp>

/*
    Function: generate_instances_output
    Objective: This function generates the XML output with all task instances, constraints, actions and valid mission
    decompositions. By valid we mean that are viable given the knowledge that we have about the world. Robot-related
    predicates are not resolved and are left as attributes to be evaluated

    @ Input 1: The Task Graph as an ATGraph object
    @ Input 2: The goal model as a GMGraph object
    @ Input 3: The output file name and path
    @ Input 4: The world state
    @ Input 5: The semantic mappings vector
	@ Input 6: The sorts map, where we have our objects
	@ Input 7: The sort definitions
	@ Input 8: The predicates definitions
    @ Output: Void. The output file is generated in the given relative path
*/
void generate_instances_output(ATGraph mission_decomposition, GMGraph gm, pair<string,string> output, vector<ground_literal> world_state, vector<SemanticMapping> semantic_mapping,
                                map<string,set<string>> sorts, vector<sort_definition> sort_definitions, vector<predicate_definition> predicate_definitions) {
    queue<pair<int,ATNode>> mission_queue = generate_mission_queue(mission_decomposition);                            
    vector<Constraint> mission_constraints = generate_at_constraints(mission_decomposition, mission_queue);

    /*
        Generate final mission constraints

        -> Here we don't include parallel constraints since we resolve context dependencies
        -> All constraints to be introduced in the output are sequential, since parallel is not a constraint by itself
            - It would be only if we had simultaneity constraints or non-overlapping constraints
    */
    vector<Constraint> final_mission_constraints = transform_at_constraints(mission_decomposition,mission_constraints,gm);

    generate_noncoop_constraints(final_mission_constraints,mission_decomposition);

    // With the final constraints and the mission decomposition graph we generate our output

    pt::ptree output_file;

    vector<vector<pair<int,ATNode>>> valid_mission_decompositions = generate_valid_mission_decompositions(mission_decomposition, final_mission_constraints, world_state);

    vector<Decomposition> task_instances;
    map<string,task> actions;

    auto nodes = vertices(mission_decomposition);

    int graph_size = *nodes.second - *nodes.first;

    for(int index = 0;index < graph_size;index++) {
        if(mission_decomposition[index].node_type == DECOMPOSITION) {
            Decomposition d = std::get<Decomposition>(mission_decomposition[index].content);

            for(task a : d.path) {
                if(actions.find(a.name) == actions.end() && a.name.find(method_precondition_action_name) == std::string::npos) {
                    actions[a.name] = a;
                }
            }

            task_instances.push_back(d);
        }
    }

    output_actions(output_file, actions);

    std::map<std::string,std::string> task_id_map = output_tasks(output_file, task_instances, semantic_mapping);

    output_constraints(output_file, final_mission_constraints, task_id_map);

    output_mission_decompositions(output_file, valid_mission_decompositions, task_id_map);
    
    string output_filename = "xml/OutputTest.xml";

    pt::xml_writer_settings<string> settings(' ',4);
    pt::write_xml(output_filename, output_file, std::locale(), settings);
}

/*
    Function: output_actions
    Objective: Insert Actions into the XML Output file

    @ Input 1: A reference to the output file ptree object
    @ Input 2: The map of actions
    @ Output: Void. The output file ptree oject is filled

    NOTES: -> Fields:
            - Name
            - Capabilities
*/
void output_actions(pt::ptree& output_file, map<string,task> actions) {
    output_file.put("actions","");

    map<string,string>actions_id_map;

    int actions_counter = 0;
    map<string,task>::iterator actions_it;
    for(actions_it = actions.begin();actions_it != actions.end();++actions_it) {
        string action_name = "actions.action" + to_string(actions_counter);
        output_file.add(action_name,"");

        string action_attr;

        action_attr = action_name + ".name";
        output_file.put(action_attr,actions_it->first);

        action_attr = action_name + ".capabilities.<xmlattr>.number";
        output_file.put(action_attr,actions_it->second.required_capabilities.size());

        int capability_counter = 0;
        for(string cap : actions_it->second.required_capabilities) {
            string cap_attr = action_name + ".capabilities.capability" + to_string(capability_counter);
            output_file.put(cap_attr,cap);

            capability_counter++;
        }

        actions_counter++;
        actions_id_map[actions_it->first] = action_name;
    }    
}

/*
    Function: output_tasks
    Objective: Insert Task Decompositions into the XML Output file

    @ Input 1: A reference to the output file ptree object
    @ Input 2: The vector of task_decompositions
    @ Input 3: The vector of semantic mappings
    @ Output: A map with the tasks XML ID's. Also, the output file ptree oject is filled

    NOTES: -> Fields:
            - ID
            - Name
            - Location
            - Preconditions
            - Effects
            - Decomposition (Into actions)
           -> Each instance corresponds to a decomposition of an AT
*/
std::map<std::string,std::string> output_tasks(pt::ptree& output_file, vector<Decomposition> task_instances, vector<SemanticMapping> semantic_mapping) {
    map<string,string> task_id_map;

    output_file.put("tasks","");

    int task_counter = 0;
    for(Decomposition instance : task_instances) {
        string task_name = "tasks.task" + to_string(task_counter);
        output_file.add(task_name,"");

        string task_attr;

        task_attr = task_name + ".id";
        output_file.put(task_attr,instance.id);

        task_attr = task_name + ".name";
        output_file.put(task_attr,instance.at.name);

        task_attr = task_name + ".locations";
        if(holds_alternative<vector<string>>(instance.at.location.first)) {
            vector<string> locations = get<vector<string>>(instance.at.location.first);
            int loc_counter = 0;
            for(string loc : locations) {
                task_attr += ".t" + to_string(loc_counter);
                output_file.put(task_attr,loc);
                
                loc_counter++;
            }
        } else {
            string location = get<string>(instance.at.location.first);
            task_attr += ".t0";
            output_file.put(task_attr,location);
        }

        task_attr = task_name + ".robots_num.<xmlattr>.fixed";
        if(instance.at.fixed_robot_num) {
            output_file.put(task_attr,"True");
            
            task_attr = task_name + ".robots_num.<xmlattr>.num";
            output_file.put(task_attr,get<int>(instance.at.robot_num));
        } else {
            output_file.put(task_attr,"False");

            pair<int,int> robot_range = get<pair<int,int>>(instance.at.robot_num);

            task_attr = task_name + ".robots_num.<xmlattr>.min";
            output_file.put(task_attr,to_string(robot_range.first));

            task_attr = task_name + ".robots_num.<xmlattr>.max";
            output_file.put(task_attr,to_string(robot_range.second));
        }

        task_attr = task_name + ".preconditions";

        int prec_index = 0;
        for(auto prec : instance.prec) {
            map<string,string> task_vars;
            for(pair<string,string> args : instance.at.at.vars) {
                task_vars[args.first] = args.second;
            }

            pair<SemanticMapping,bool> prec_mapping = find_predicate_mapping(prec,semantic_mapping,sorts,task_vars,sort_definitions);

            if(!prec_mapping.second) {
                std::string semantic_mapping_error;
                if(holds_alternative<ground_literal>(prec)) {
                    ground_literal p = get<ground_literal>(prec);
                    semantic_mapping_error += "No Semantic Mapping exists for predicate [" + p.predicate + " ";
                    unsigned int index = 0;
                    for(string arg : p.args) {
                        if(index == p.args.size()-1) {
                            semantic_mapping_error += arg + "] ";
                        } else {
                            semantic_mapping_error += arg + " "; 
                        }
                    }
                } else {
                    literal p = get<literal>(prec);
                    semantic_mapping_error = "No Semantic Mapping exists for predicate [" + p.predicate + " ";
                    unsigned int index = 0;
                    for(string arg : p.arguments) {
                        if(index == p.arguments.size()-1) {
                            semantic_mapping_error += arg + "] ";
                        } else {
                            semantic_mapping_error += arg + " "; 
                        }
                    }
                }
                semantic_mapping_error += "when trying to generate output for task " + instance.id + ": " + instance.at.name;
                throw std::runtime_error(semantic_mapping_error);
            } else {
                /*
                    Here we output the predicate as an attribute in the preconditions attribute of the XML

                    -> For now we only map predicates to attributes. These predicates have only one argument
                */
                string prec_output;

                if(prec_mapping.first.get_mapping_type() == "attribute") {  
                    vector<string> arguments;
                    string prec_name;
                    if(holds_alternative<ground_literal>(prec)) {
                        ground_literal p = get<ground_literal>(prec);

                        if(!p.positive) prec_output += "not ";
                        prec_output += p.args.at(0) + ".";
                        arguments = p.args;
                        prec_name = p.predicate;
                    } else {
                        literal p = get<literal>(prec);

                        if(!p.positive) prec_output += "not ";
                        prec_output += p.arguments.at(0) + ".";
                        arguments = p.arguments;
                        prec_name = p.predicate;
                    }
                    prec_output += get<string>(prec_mapping.first.get_prop("name"));

                    vector<string> arg_sorts;
                    for(predicate_definition pred : predicate_definitions) {
                        if(pred.name == prec_name) {
                            arg_sorts = pred.argument_sorts;
                            break;
                        }
                    }


                    string prec_name_xml = task_attr + ".prec" + to_string(prec_index);

                    string prec_attr_xml = prec_name_xml + ".<xmlattr>.vars";
                    string vars_list;
                    for(unsigned int i = 0;i < arguments.size();i++) {
                        if(i == arguments.size()-1) {
                            vars_list += arguments.at(i);
                        } else {
                            vars_list += arguments.at(i) + ",";
                        }
                    }
                    output_file.put(prec_attr_xml,vars_list);

                    prec_attr_xml = prec_name_xml + ".<xmlattr>.var_types";
                    string var_types_list;
                    for(unsigned int i = 0;i < arg_sorts.size();i++) {
                        if(i == arg_sorts.size()-1) {
                            var_types_list += arg_sorts.at(i);
                        } else {
                            var_types_list += arg_sorts.at(i) + ",";
                        }
                    }
                    output_file.put(prec_attr_xml,var_types_list);

                    output_file.put(prec_name_xml,prec_output);
                }

                prec_index++;
            }
        }

        /*
            For effects we have to verify how we dealt with non ground literals so that we can try to verify the types
        */
        task_attr = task_name + ".effects";

        int eff_index = 0;
        for(auto eff : instance.eff) {
            
            map<string,string> task_vars;
            for(pair<string,string> args : instance.at.at.vars) {
                task_vars[args.first] = args.second;
            }

            pair<SemanticMapping,bool> eff_mapping = find_predicate_mapping(eff,semantic_mapping,sorts,task_vars,sort_definitions);

            if(!eff_mapping.second) {
                std::string semantic_mapping_error;
                if(holds_alternative<ground_literal>(eff)) {
                    ground_literal e = get<ground_literal>(eff);
                    semantic_mapping_error += "No Semantic Mapping exists for predicate [" + e.predicate + " ";
                    unsigned int index = 0;
                    for(string arg : e.args) {
                        if(index == e.args.size()-1) {
                            semantic_mapping_error += arg + "] ";
                        } else {
                            semantic_mapping_error += arg + " "; 
                        }
                    }
                } else {
                    literal e = get<literal>(eff);
                    semantic_mapping_error += "No Semantic Mapping exists for predicate [" + e.predicate + " ";
                    unsigned int index = 0;
                    for(string arg : e.arguments) {
                        if(index == e.arguments.size()-1) {
                            semantic_mapping_error += arg + "] ";
                        } else {
                            semantic_mapping_error += arg + " "; 
                        }
                    }
                }
                semantic_mapping_error += "when trying to generate output for task " + instance.id + ": " + instance.at.name;
                throw std::runtime_error(semantic_mapping_error);
            } else {
                /*
                    Here we output the predicate as an attribute in the preconditions attribute of the XML

                    -> For now we only map predicates to attributes. These predicates have only one argument
                */
                string eff_output;

                if(eff_mapping.first.get_mapping_type() == "attribute") {   
                    vector<string> arguments;
                    string eff_name;
                    if(holds_alternative<ground_literal>(eff)) {
                        ground_literal e = get<ground_literal>(eff);

                        if(!e.positive) eff_output += "not ";
                        eff_output += e.args.at(0) + ".";
                        arguments = e.args;
                        eff_name = e.predicate;
                    } else {
                        literal e = get<literal>(eff);

                        if(!e.positive) eff_output += "not ";
                        eff_output += e.arguments.at(0) + ".";
                        arguments = e.arguments;
                        eff_name = e.predicate;
                    }
                    eff_output += get<string>(eff_mapping.first.get_prop("name"));

                    vector<string> arg_sorts;
                    for(predicate_definition pred : predicate_definitions) {
                        if(pred.name == eff_name) {
                            arg_sorts = pred.argument_sorts;
                            break;
                        }
                    }

                    string eff_name_xml = task_attr + ".eff" + to_string(eff_index);

                    string eff_attr_xml = eff_name_xml + ".<xmlattr>.vars";
                    string vars_list;
                    for(unsigned int i = 0;i < arguments.size();i++) {
                        if(i == arguments.size()-1) {
                            vars_list += arguments.at(i);
                        } else {
                            vars_list += arguments.at(i) + ",";
                        }
                    }
                    output_file.put(eff_attr_xml,vars_list);

                    eff_attr_xml = eff_name_xml + ".<xmlattr>.var_types";
                    string var_types_list;
                    for(unsigned int i = 0;i < arg_sorts.size();i++) {
                        if(i == arg_sorts.size()-1) {
                            var_types_list += arg_sorts.at(i);
                        } else {
                            var_types_list += arg_sorts.at(i) + ",";
                        }
                    }
                    output_file.put(eff_attr_xml,var_types_list);

                    output_file.put(eff_name_xml,eff_output);
                }

                eff_index++;
            }
        }
        
        /*
            Here we output the list of triggering events
        */
        task_attr = task_name + ".triggering_events";
        int event_counter = 0;
        for(string e : instance.at.triggering_events) {
            string event_id = task_attr + ".event" + to_string(event_counter);
            output_file.put(event_id,e);

            event_counter++;
        }

        /*
            Introducing the decomposition using actions

            -> For now we put action names but we should instantiate actions in the XML first so we can have a map to their id's in the XML file
        */
        task_attr = task_name + ".decomposition";
        int action_counter = 0;
        for(task a : instance.path) {
            if(a.name.find(method_precondition_action_name) == std::string::npos) {
                string action_id = task_attr + ".action" + to_string(action_counter);
                output_file.put(action_id,a.name);

                action_counter++;
            }
        }

        size_t id_separator = task_name.find(".");
        task_id_map[instance.id] = task_name.substr(id_separator+1);
        task_counter++;
    }

    return task_id_map;
}

/*
    Function: output_constraints
    Objective: Insert Constraints into the XML Output file

    @ Input 1: A reference to the output file ptree object
    @ Input 2: The final mission constraints
    @ Input 3: The map of task XML ID's
    @ Output: Void. The output file ptree oject is filled

    NOTES: -> Fields:
            - Type
            - Task Instances
            - Group (Important only if constraint is of NC type)
            - Divisible (Important only if constraint is of NC type)
*/
void output_constraints(pt::ptree& output_file, vector<Constraint> final_mission_constraints, std::map<std::string,std::string> task_id_map) {
    output_file.put("constraints","");
    
    int constraint_counter = 0;
    for(Constraint c : final_mission_constraints) {
        string constraint_name = "constraints.constraint" + to_string(constraint_counter);

        string constraint_attr = constraint_name + ".type";
        if(c.type == SEQ) {
            output_file.put(constraint_attr,"SEQ");
        } else {
            output_file.put(constraint_attr,"EC");
        }

        constraint_attr = constraint_name + ".task_instances.id0";

        string t_id = get<Decomposition>(c.nodes_involved.first.second.content).id;
        output_file.put(constraint_attr,task_id_map[t_id]);

        constraint_attr = constraint_name + ".task_instances.id1";

        t_id = get<Decomposition>(c.nodes_involved.second.second.content).id;
        output_file.put(constraint_attr,task_id_map[t_id]);

        if(c.type == NC) {
            string attr_value;

            constraint_attr = constraint_name + ".group";
            if(c.group) {
                attr_value = "True";
            } else {
                attr_value = "False";
            }
            output_file.put(constraint_attr,attr_value);

            constraint_attr = constraint_name + ".divisible";
            if(c.divisible) {
                attr_value = "True";
            } else {
                attr_value = "False";
            }
            output_file.put(constraint_attr,attr_value);
        }
        constraint_counter++;
    }
}

/*
    Function: output_mission_decompositions
    Objective: Insert Mission Decompositions into the XML Output file

    @ Input 1: A reference to the output file ptree object
    @ Input 2: The valid mission decompositions
    @ Input 3: The map of task XML ID's
    @ Output: Void. The output file ptree oject is filled

    NOTES: -> Fields:
            - Decomposition
            - Task Instances
*/
void output_mission_decompositions(pt::ptree& output_file, std::vector<std::vector<std::pair<int,ATNode>>> valid_mission_decompositions, std::map<std::string,std::string> task_id_map) {
    output_file.put("mission_decompositions","");

    int decomposition_counter = 0;
    for(vector<pair<int,ATNode>> mission_decomposition : valid_mission_decompositions) {
        string decomposition_name = "mission_decompositions.decomposition" + to_string(decomposition_counter);
        int task_counter = 0;
        for(pair<int,ATNode> task : mission_decomposition) {
            string task_name = decomposition_name + ".tasks.t" + to_string(task_counter);

            output_file.put(task_name,task_id_map[get<Decomposition>(task.second.content).id]);
            task_counter++;
        }

        decomposition_counter++;
    }
}

/*
    Function: find_predicate_mapping
    Objective: Find a semantic mapping involving a given predicate

    @ Input 1: The predicate to be evaluated
    @ Input 2: The vector of semantic mappings
    @ Input 3: The sorts map, where objects are declared
    @ Input 4: The var mappings between HDDL and OCL goal model variables
    @ Input 5: The sort definitions
    @ Output: A pair containing the semantic mapping and a boolean flag indicating if a mapping was found
*/
pair<SemanticMapping, bool> find_predicate_mapping(variant<ground_literal,literal> predicate, vector<SemanticMapping> semantic_mappings, map<string,set<string>> sorts,
                                                    map<string,string> vars, vector<sort_definition> sort_definitions) {
    SemanticMapping prec_mapping;
    bool found_mapping = false;

    if(holds_alternative<ground_literal>(predicate)) {
        /*
            If the predicate is grounded we can search in the declared objects for the necessary attributes
        */
        ground_literal p = get<ground_literal>(predicate);

        for(SemanticMapping sm : semantic_mappings) {
            if(sm.get_mapped_type() == "predicate") {
                predicate_definition map = get<predicate_definition>(sm.get_prop("map"));

                if(map.name == p.predicate) {
                    bool found_args = true;
                    int arg_index = 0;
                    for(string sort : map.argument_sorts) {
                        bool found_arg = false;
                        for(string object : sorts[sort]) {
                            if(object == p.args.at(arg_index)) {
                                found_arg = true;
                                break;
                            }
                        }

                        if(!found_arg) {
                            found_args = false;
                            break;
                        }
                    }

                    if(found_args) { 
                        prec_mapping = sm;
                        found_mapping = true;
                        break;
                    }
                }
            }
        }
    } else {
        literal p = get<literal>(predicate);

        for(SemanticMapping sm : semantic_mappings) {
            if(sm.get_mapped_type() == "predicate") {
                predicate_definition map = get<predicate_definition>(sm.get_prop("map"));

                if(map.name == p.predicate) {
                    bool found_args = true;
                    int arg_index = 0;
                    for(string sort : map.argument_sorts) {
                        bool found_arg = false;
                        
                        /*
                            Here we need to check if the predicate literal is equal to the predicate in the mapping

                            -> In order to be equal, we need to have the same predicate and the same argument types
                        */
                        if(vars[p.arguments.at(arg_index)] == sort) {
                            found_arg = true;
                        } else {
                            bool is_parent_type = false;
                            for(sort_definition s : sort_definitions) {
                                for(string d_sort : s.declared_sorts) {
                                    if(d_sort == vars[p.arguments.at(arg_index)]) {
                                        if(s.has_parent_sort) {
                                            if(s.parent_sort == sort) {
                                                is_parent_type = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                if(is_parent_type) {
                                    found_arg = true;
                                    break;
                                }
                            }
                        }

                        if(!found_arg) {
                            found_args = false;
                            break;
                        }

                        arg_index++;
                    }

                    if(found_args) { 
                        prec_mapping = sm;
                        found_mapping = true;
                        break;
                    }
                }
            }
        }
    }

    return make_pair(prec_mapping, found_mapping);
}

/*
    Function: generate_valid_mission_decompositions
    Objective: Generate the valid mission decompositions based on constraints and on the world knowledge. This
    function iniatilizes variables based on the root node and calls a recursive function that performs the generation

    @ Input 1: The Task Graph as an ATGraph object
    @ Input 2: The vector of constraints
    @ Input 3: The world state
    @ Output: The valid mission decompositions vector. A mission decomposition is a vector of pairs of the 
    form ([task_id],[task_node])
*/
vector<vector<pair<int,ATNode>>> generate_valid_mission_decompositions(ATGraph mission_decomposition, vector<Constraint> mission_constraints, vector<ground_literal> world_state) {
    vector<pair<vector<pair<int,ATNode>>,vector<ground_literal>>> valid_mission_decompositions;

    queue<pair<int,ATNode>> mission_queue = generate_mission_queue(mission_decomposition);

    /*
        The idea here is:

        -> Go through the queue and recursively build the decompositions (valid ones)
        -> The initial state of the world will be updated depending if we have a sequential or a parallel operator
        
        -> When we find an operator, we need to establish the relation between previous found nodes
        -> If the operator succeeds another operator, we know that we need to relate a task with task already involved in another constraint
        -> If the operator succeds a task, we know that this operator relates to the last two tasks
    */
    vector<pair<int,ATNode>> possible_conflicts;
    recursive_valid_mission_decomposition(mission_decomposition, world_state, mission_constraints, "", mission_queue, valid_mission_decompositions, possible_conflicts);

    vector<vector<pair<int,ATNode>>> final_valid_mission_decompositions;
    for(auto mission_decomposition : valid_mission_decompositions) {
        final_valid_mission_decompositions.push_back(mission_decomposition.first);
    }

    std::cout << "Valid Mission Decompositions: " << std::endl;
    for(auto mission_decomposition : final_valid_mission_decompositions) {
        std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl;
        unsigned int index = 1;
        for(pair<int,ATNode> node : mission_decomposition) {
            if(index == mission_decomposition.size()) {
                std::cout << get<Decomposition>(node.second.content).id << std::endl;
            } else {
                std::cout << get<Decomposition>(node.second.content).id << " -> ";
            }
            index++;
        }
        std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl << std::endl;
    }

    return final_valid_mission_decompositions;
}

/*
    Function: recursive_valid_mission_decomposition
    Objective: Generate the valid mission decompositions based on constraints and on the world knowledge. This is the
    recursive function that in fact generates them.

    @ Input 1: The Task Graph as an ATGraph object
    @ Input 2: The initial world state before visiting the task nodes
    @ Input 3: The vector of constraints
    @ Input 4: The last operation found in the recursive calls
    @ Input 5: A reference to the mission queue
    @ Input 6: The vector of valid mission decompositions
    @ Input 7: The possible conflicts that need to be analyzed
    @ Output: Void. The valid mission decompositions will be generated
*/
void recursive_valid_mission_decomposition(ATGraph mission_decomposition, vector<ground_literal> initial_world_state, vector<Constraint> mission_constraints, string last_op,
                                            queue<pair<int,ATNode>>& mission_queue, vector<pair<vector<pair<int,ATNode>>,vector<ground_literal>>>& valid_mission_decompositions,
                                                vector<pair<int,ATNode>>& possible_conflicts) {
    /*
        Here we will get the current node and check whether it is an operator or an Abstract Task
    */
   pair<int,ATNode> current_node = mission_queue.front();
   mission_queue.pop();

   if(holds_alternative<string>(current_node.second.content)) {
       /*
            If we have an operator, we need to check if it is parallel or sequential

            -> The behavior is different depending on which operator we are dealing with
       */
        string op = get<string>(current_node.second.content);

        if(op == "#") {
            /*
                If the operator is parallel we:

                -> Go through the queue while the next node in the queue is a child of this operator
                    - This is done checking the out edges of the parallel operator node and verifying if the
                    node in the queue is present
                -> For each child we recursively perform decomposition
            */
            bool checking_children = true;

            while(checking_children) {
                if(mission_queue.size() == 0) {
                    break;
                }
                pair<int,ATNode> next_node = mission_queue.front();

                bool is_child = false;

                ATGraph::out_edge_iterator ei, ei_end;
                for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
                    int source = boost::source(*ei,mission_decomposition);
                    int target = boost::target(*ei,mission_decomposition);
                    auto edge = boost::edge(source,target,mission_decomposition).first;

                    int children_num = 0;
                    ATGraph::out_edge_iterator target_ei, target_ei_end;
                    for(boost::tie(target_ei,target_ei_end) = out_edges(target,mission_decomposition);target_ei != target_ei_end;++target_ei) {
                        children_num++;
                    }

                    /*
                        If we have a goal node as child we have to search its children for the next node
                    */
                    if(mission_decomposition[target].node_type == GOALNODE || (mission_decomposition[target].node_type == OP && children_num < 2)) {
                        bool goal_node = true;
                        int child_id = target;
                        while(goal_node) {
                            ATGraph::out_edge_iterator ci, ci_end;
                            for(boost::tie(ci,ci_end) = out_edges(child_id,mission_decomposition);ci != ci_end;++ci) {
                                int s = boost::source(*ci,mission_decomposition);
                                int t = boost::target(*ci,mission_decomposition);
                                auto e = boost::edge(s,t,mission_decomposition).first;

                                /*
                                    A goal node only has a goal as child if it has a Means-end decomposition
                                */
                               if(mission_decomposition[t].node_type != GOALNODE) {
                                    goal_node = false;
                                    if(mission_decomposition[e].edge_type == NORMAL) {
                                        if(t == next_node.first) {
                                            is_child = true;
                                            break;
                                        }
                                    }
                                } else {
                                    child_id = t;
                                }
                            }
                        }

                        if(is_child) {
                            break;
                        }
                    } else {
                        if(mission_decomposition[edge].edge_type == NORMAL) {
                            if(target == next_node.first) {
                                is_child = true;
                                break;
                            }
                        }
                    }
                }

                if(is_child) {
                    recursive_valid_mission_decomposition(mission_decomposition,initial_world_state,mission_constraints,"#",mission_queue,valid_mission_decompositions,possible_conflicts);
                } else {
                    checking_children = false;
                }
            }
        } else if(op == ";") {
            /*
                Here we have to deal with the possible conflicts and then erase them
            */
            if(possible_conflicts.size() > 1) {
                std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
                std::cout << "Resolving conflicts..." << std::endl;
                std::cout << "Possible conflicts: " << std::endl;
                for(pair<int,ATNode> t : possible_conflicts) {
                    std::cout << std::get<AbstractTask>(t.second.content).name << std::endl;
                }
                std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
                resolve_conflicts(valid_mission_decompositions, possible_conflicts, mission_decomposition, mission_constraints);
                possible_conflicts.clear();
            }

            bool checking_children = true;
            while(checking_children) {
                if(mission_queue.size() == 0) {
                    break;
                }
                pair<int,ATNode> next_node = mission_queue.front();
                bool is_child = false;

                ATGraph::out_edge_iterator ei, ei_end;
                for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
                    int source = boost::source(*ei,mission_decomposition);
                    int target = boost::target(*ei,mission_decomposition);
                    auto edge = boost::edge(source,target,mission_decomposition).first;

                    int children_num = 0;
                    ATGraph::out_edge_iterator target_ei, target_ei_end;
                    for(boost::tie(target_ei,target_ei_end) = out_edges(target,mission_decomposition);target_ei != target_ei_end;++target_ei) {
                        children_num++;
                    }

                    if(mission_decomposition[target].node_type == GOALNODE || (mission_decomposition[target].node_type == OP && children_num < 2)) {
                        bool goal_node = true;
                        int child_id = target;
                        while(goal_node) {
                            ATGraph::out_edge_iterator ci, ci_end;
                            for(boost::tie(ci,ci_end) = out_edges(child_id,mission_decomposition);ci != ci_end;++ci) {
                                int s = boost::source(*ci,mission_decomposition);
                                int t = boost::target(*ci,mission_decomposition);
                                auto e = boost::edge(s,t,mission_decomposition).first;

                                /*
                                    A goal node only has a goal as child if it has a Means-end decomposition
                                */
                               if(mission_decomposition[t].node_type != GOALNODE) {
                                    goal_node = false;
                                    if(mission_decomposition[e].edge_type == NORMAL) {
                                        if(t == next_node.first) {
                                            is_child = true;
                                            break;
                                        }
                                    }
                                } else {
                                    child_id = t;
                                }
                            }
                        }

                        if(is_child) {
                            break;
                        }
                    } else {
                        if(mission_decomposition[edge].edge_type == NORMAL) {
                            if(target == next_node.first) {
                                is_child = true;
                                break;
                            }
                        }
                    }
                }

                if(is_child) {
                    recursive_valid_mission_decomposition(mission_decomposition,initial_world_state,mission_constraints,";",mission_queue,valid_mission_decompositions,possible_conflicts);
                } else {
                    checking_children = false;
                }
            }
        }
    } else {
       /*
            If we have an AT we have to do the following for each decomposition

            -> For each valid mission decomposition, check if the world state satisfies the decomposition precondition
                - If there is no valid mission decomposition, just check against the initial world state
            -> If the world state satisfies the preconditions, add the task to the valid mission decomposition
                - If not, we will still need to check if some task in the valid mission decomposition is involved in a sequential constraint
                with the actual AT. This relates to Context Dependencies
                - If the effect of this task makes the world state satisfy the precondition, add this decomposition in the valid mission decomposition
            
            -> NOTE: IF A TASK DOES NOT HAVE ANY VALID DECOMPOSITION WE RAISE AN ERROR SINCE FOR NOW WE HAVE ONLY AND TASKS IN THE GOAL MODEL
       */
        vector<pair<int,ATNode>> task_decompositions;

        ATGraph::out_edge_iterator ei, ei_end;
        for(boost::tie(ei,ei_end) = out_edges(current_node.first,mission_decomposition);ei != ei_end;++ei) {
            int target = boost::target(*ei,mission_decomposition);

            if(mission_decomposition[target].node_type == DECOMPOSITION) {
                ATNode d = mission_decomposition[target];

                task_decompositions.push_back(make_pair(target,d));
            }
        }

        bool add_to_possible_conflicts = false;
        if(valid_mission_decompositions.size() > 0) {
            /*
                Here is the case where we have at least one valid mission decomposition

                -> We must have at least one decomposition valid for each valid mission decomposition
                    - If we don't have, we must raise an error
            */
            vector<pair<vector<pair<int,ATNode>>,vector<ground_literal>>> new_valid_mission_decompositions;
            for(auto valid_mission_decomposition : valid_mission_decompositions) {
                bool valid_task_decomposition = false; //Check if one task decomposition is valid for this mission decomposition

                for(pair<int,ATNode> task_decomposition : task_decompositions) {
                    Decomposition d = get<Decomposition>(task_decomposition.second.content);
                    
                    vector<ground_literal> world_state = valid_mission_decomposition.second;
                    vector<pair<int,ATNode>> mission_decomposition = valid_mission_decomposition.first;

                    bool preconditions_hold = true;
                    for(auto prec : d.prec) {
                        if(holds_alternative<ground_literal>(prec)) {
                            ground_literal p = get<ground_literal>(prec);
                            
                            for(ground_literal state : world_state) {
                                if(state.predicate == p.predicate) {
                                    bool equal_args = true;

                                    int index = 0;
                                    for(string arg : state.args) {
                                        if(arg != p.args.at(index)) {
                                            equal_args = false;
                                            break;
                                        }
                                    }

                                    if(equal_args) {
                                        if(state.positive != p.positive) {
                                            preconditions_hold = false;
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        if(!preconditions_hold) {
                            break;
                        }
                    }

                    if(preconditions_hold) {
                        /*
                            If preconditions hold we create a new valid mission decomposition with this task decomposition added to the mission decomposition

                            -> If we have a parallel operator we do not update the world state and add the AT to the possible conflicts
                            -> If we have a sequential operator we update the world state and put it into the valid mission decomposition
                        */
                        mission_decomposition.push_back(task_decomposition);

                        if(last_op == "#") {
                            new_valid_mission_decompositions.push_back(make_pair(mission_decomposition,world_state));
                            add_to_possible_conflicts = true;
                        } else if(last_op == ";") {
                            //Update initial world state
                            vector<ground_literal> updated_state = world_state;
                            for(auto eff : d.eff) {
                                if(holds_alternative<ground_literal>(eff)) {
                                    ground_literal e = get<ground_literal>(eff);

                                    for(ground_literal& state : updated_state) {
                                        if(state.predicate == e.predicate) {
                                            bool equal_args = true;

                                            int index = 0;
                                            for(string arg : state.args) {
                                                if(arg != e.args.at(index)) {
                                                    equal_args = false;
                                                    break;
                                                }
                                            }

                                            if(equal_args) {
                                                if(state.positive != e.positive) {
                                                    state.positive = e.positive;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            new_valid_mission_decompositions.push_back(make_pair(mission_decomposition,updated_state));
                        }

                        valid_task_decomposition = true;
                    } else {
                        /*
                            Here we have to put the code for checking for context dependecies

                            -> We need to check for the possible conflicts
                            -> If there is any we check the constraints to see if we can add the task decomposition to the mission decomposition or not
                                - If we can, everything is fine and we add it
                                - If we can't move on to the next
                        */
                    }
                }

                if(!valid_task_decomposition) {
                    AbstractTask at = get<AbstractTask>(current_node.second.content);
                    std::string invalid_task_decomposition_error = "NO VALID DECOMPOSITIONS FOR TASK " + at.id + ": " + at.name;
                    
                    throw std::runtime_error(invalid_task_decomposition_error);
                }
            }

            valid_mission_decompositions = new_valid_mission_decompositions;
        } else {
            //Here is the case where we have no valid mission decompositions yet
            bool at_least_one_decomposition_valid = false;

            for(pair<int,ATNode> task_decomposition : task_decompositions) {
                //Check preconditions using the initial world state
                Decomposition d = get<Decomposition>(task_decomposition.second.content);
                bool preconditions_hold = true;
                for(auto prec : d.prec) {
                    if(holds_alternative<ground_literal>(prec)) {
                        ground_literal p = get<ground_literal>(prec);
                        
                        for(ground_literal state : initial_world_state) {
                            if(state.predicate == p.predicate) {
                                bool equal_args = true;

                                int index = 0;
                                for(string arg : state.args) {
                                    if(arg != p.args.at(index)) {
                                        equal_args = false;
                                        break;
                                    }
                                }

                                if(equal_args) {
                                    if(state.positive != p.positive) {
                                        preconditions_hold = false;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if(!preconditions_hold) {
                        break;
                    }
                }

                if(preconditions_hold) {
                    /*
                        If preconditions hold we create a new valid mission decomposition

                        -> If we have a parallel operator we do not update the world state and add the AT to the possible conflicts
                        -> If we have a sequential operator we update the world state and put it into the valid mission decomposition
                    */
                    AbstractTask at1 = get<AbstractTask>(current_node.second.content);
                    pair<vector<pair<int,ATNode>>,vector<ground_literal>> new_valid_mission;

                    vector<pair<int,ATNode>> new_decomposition;
                    new_decomposition.push_back(task_decomposition);

                    if(last_op == "#") {
                        new_valid_mission = make_pair(new_decomposition,initial_world_state);
                        add_to_possible_conflicts = true;
                    } else if(last_op == ";") {
                        //Update initial world state
                        vector<ground_literal> updated_state = initial_world_state;
                        for(auto eff : d.eff) {
                            if(holds_alternative<ground_literal>(eff)) {
                                ground_literal e = get<ground_literal>(eff);

                                for(ground_literal& state : updated_state) {
                                    if(state.predicate == e.predicate) {
                                        bool equal_args = true;

                                        int index = 0;
                                        for(string arg : state.args) {
                                            if(arg != e.args.at(index)) {
                                                equal_args = false;
                                                break;
                                            }
                                        }

                                        if(equal_args) {
                                            if(state.positive != e.positive) {
                                                state.positive = e.positive;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        new_valid_mission = make_pair(new_decomposition,updated_state);
                    }

                    valid_mission_decompositions.push_back(new_valid_mission);
                    at_least_one_decomposition_valid = true;
                } 
            }

            if(!at_least_one_decomposition_valid) {
                AbstractTask at = get<AbstractTask>(current_node.second.content);
                std::string invalid_task_decomposition_error = "NO VALID DECOMPOSITIONS FOR TASK " + at.id + ": " + at.name;
                
                throw std::runtime_error(invalid_task_decomposition_error);
            }
        }

        if(add_to_possible_conflicts) {
            possible_conflicts.push_back(current_node);
        }
   }
}

/*
    Function: generate_mission_queue
    Objective: Generate the mission queue based on the Task Graph

    @ Input: The Task Graph as an ATGraph object
    @ Output: The generated mission queue
*/
queue<pair<int,ATNode>> generate_mission_queue(ATGraph mission_decomposition) {
    auto nodes = vertices(mission_decomposition);

    int graph_size = *nodes.second - *nodes.first;

    /*
        Go through the graph in a DFS order and put nodes in a queue

        -> Goal Nodes are not considered
        -> Operator nodes with only one child are not considered
    */
    queue<pair<int,ATNode>> mission_queue;

    //Populate the mission queue
    for(int i = 0;i < graph_size;i++) {
        if(mission_decomposition[i].node_type == ATASK) {
            mission_queue.push(make_pair(i,mission_decomposition[i]));
        } else if(mission_decomposition[i].node_type == OP) {
            int out_edge_num = 0;
            ATGraph::out_edge_iterator ei, ei_end;

            //Only insert OP nodes that have more than one outer edge of normal type (more than one child)
            for(boost::tie(ei,ei_end) = out_edges(i,mission_decomposition);ei != ei_end;++ei) {
                auto source = boost::source(*ei,mission_decomposition);
                auto target = boost::target(*ei,mission_decomposition);
                auto edge = boost::edge(source,target,mission_decomposition);

                if(mission_decomposition[edge.first].edge_type == NORMAL) {
                    out_edge_num++;
                }
            }

            if(out_edge_num > 1) {
                mission_queue.push(make_pair(i,mission_decomposition[i]));
            }
        }
    }

    queue<pair<int,ATNode>> queue_copy = mission_queue;
    std::cout << "Mission Queue" << std::endl;
    while(!queue_copy.empty()) {
        pair<int,ATNode> node = queue_copy.front();
        queue_copy.pop(); 
        if(node.second.node_type == ATASK) {
            std::cout << "ATASK: " << get<AbstractTask>(node.second.content).id << " - " << get<AbstractTask>(node.second.content).name << std::endl;
        } else {
            std::cout << "OPERATOR: " << get<std::string>(node.second.content) << std::endl;
        }
    }

    return mission_queue;
}

/*
    Function: resolve_conflicts
    Objective: Resolve possible conflicts and add actual conflicts to the conflicts vector. If conflicts appears
    we must remove valid decompositions that contain conflicting tasks

    @ Input 1: A reference to the vector of valid mission decompositions
    @ Input 2: The vector of possible conflicts
    @ Input 3: The Task Graph as an ATGraph object
    @ Input 4: The vector of mission constraints
    @ Output: Void. The valid mission decompositions may be trimmed

    NOTES: Here we do not consider conditional effects yet!
*/

void resolve_conflicts(vector<pair<vector<pair<int,ATNode>>,vector<ground_literal>>>& valid_mission_decompositions, vector<pair<int,ATNode>> possible_conflicts,
                        ATGraph mission_decomposition, vector<Constraint> mission_constraints) {
    vector<pair<pair<int,ATNode>,pair<int,ATNode>>> actual_conflicts;

    map<int,unsigned int> task_decompositions_number;

    for(unsigned int i = 0; i < possible_conflicts.size()-1; i++) {
        for(unsigned int j = i+1; j < possible_conflicts.size(); j++) {
            pair<int,ATNode> t1 = possible_conflicts.at(i);
            pair<int,ATNode> t2 = possible_conflicts.at(j);

            vector<pair<int,ATNode>> t1_decompositions;
            ATGraph::out_edge_iterator ei, ei_end;
            for(boost::tie(ei,ei_end) = out_edges(t1.first,mission_decomposition);ei != ei_end;++ei) {
                int target = boost::target(*ei,mission_decomposition);

                if(mission_decomposition[target].node_type == DECOMPOSITION) {
                    ATNode d = mission_decomposition[target];

                    t1_decompositions.push_back(make_pair(target,d));
                }
            }
            task_decompositions_number[t1.first] = t1_decompositions.size();

            vector<pair<int,ATNode>> t2_decompositions;
            for(boost::tie(ei,ei_end) = out_edges(t2.first,mission_decomposition);ei != ei_end;++ei) {
                int target = boost::target(*ei,mission_decomposition);

                if(mission_decomposition[target].node_type == DECOMPOSITION) {
                    ATNode d = mission_decomposition[target];

                    t2_decompositions.push_back(make_pair(target,d));
                }
            }
            task_decompositions_number[t2.first] = t2_decompositions.size();

            for(unsigned int k1 = 0; k1 < t1_decompositions.size(); k1++) {
                for(unsigned int k2 = 0; k2 < t2_decompositions.size(); k2++) {
                    pair<int,Decomposition> d1 = make_pair(t1_decompositions.at(k1).first, std::get<Decomposition>(t1_decompositions.at(k1).second.content));
                    pair<int,Decomposition> d2 = make_pair(t2_decompositions.at(k2).first, std::get<Decomposition>(t2_decompositions.at(k2).second.content));

                    bool is_non_divisible_or_non_group = false;
                    for(Constraint c : mission_constraints) {
                        if(c.type == NC) {
                            if(c.nodes_involved.first.first == d1.first) {
                                if(c.nodes_involved.second.first == d2.first) {
                                    if(!c.group || !c.divisible) {
                                        is_non_divisible_or_non_group = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    /*
                        We need to check:
                            -> ground_literal effects for every decomposition pair
                            -> robot-related literal effects if is_non_divisible_or_non_group is true 

                        How do we check if effects have the same arguments?
                            - If it is constant, no problem
                            - If it is a variable and it is robottype or robot:
                                * Evaluate is_non_divisible_or_non_group and then
                                proceed to verify equality
                    */
                    bool has_conflict = false;
                    for(auto eff1 : d1.second.eff) {
                        for(auto eff2 : d2.second.eff) {
                            if(holds_alternative<ground_literal>(eff1)) {
                                if(holds_alternative<ground_literal>(eff2)) {
                                    ground_literal e1 = std::get<ground_literal>(eff1);
                                    ground_literal e2 = std::get<ground_literal>(eff2);

                                    if(e1.predicate == e2.predicate) {
                                        bool equal_args = true;
                                        for(unsigned int arg_index = 0; arg_index < e1.args.size(); arg_index++) {
                                            if(e1.args.at(arg_index) != e2.args.at(arg_index)) {
                                                equal_args = false;
                                                break;
                                            }
                                        }
                                        if(equal_args) {
                                            if(e1.positive != e2.positive) {
                                                has_conflict = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            } else if(holds_alternative<literal>(eff1)) {
                                if(holds_alternative<literal>(eff2)) {
                                    literal e1 = std::get<literal>(eff1);
                                    literal e2 = std::get<literal>(eff2);

                                    if(e1.predicate == e2.predicate) {
                                        bool equal_args = true;
                                        for(unsigned int arg_index = 0; arg_index < e1.arguments.size(); arg_index++) {
                                            if(e1.arguments.at(arg_index).rfind("?",0) == 0) { // Argument is variable
                                                if(e2.arguments.at(arg_index).rfind("?",0) == 0) {
                                                    // Find argument in abstract task definition
                                                    task at_def = d1.second.at.at;
                                                    for(pair<string,string> var : at_def.vars) {
                                                        if(var.first == e1.arguments.at(arg_index)) {
                                                            if(var.second == "robot" || var.second == "robotteam") {
                                                                if(!is_non_divisible_or_non_group) {
                                                                    equal_args = false;
                                                                }
                                                            }
                                                            break;
                                                        }
                                                    }
                                                    if(!equal_args) {
                                                        break;
                                                    }
                                                }
                                            } else {
                                                if(e1.arguments.at(arg_index) != e2.arguments.at(arg_index)) {
                                                    equal_args = false;
                                                    break;
                                                }
                                            }
                                        }
                                        if(equal_args) {
                                            if(e1.positive != e2.positive) {
                                                has_conflict = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if(has_conflict) {
                            break;
                        }
                    }
                    if(has_conflict) {
                        actual_conflicts.push_back(make_pair(t1_decompositions.at(k1),t2_decompositions.at(k2)));
                    }
                }
            }

            /*
                With the actual conflicts found, we have to check the valid decompositions
            */
            if(actual_conflicts.size() > 0) {
                map<int,vector<int>> found_task_decompositions;
                for(pair<pair<int,ATNode>,pair<int,ATNode>> conflict : actual_conflicts) {
                    found_task_decompositions[t1.first].push_back(conflict.first.first);
                    found_task_decompositions[t2.first].push_back(conflict.second.first);

                    // If all the decompositions of a task are in conflict, throw an error
                    if(found_task_decompositions[t1.first].size() == task_decompositions_number[t1.first]) {
                        std::string conflict_error = "Cannot solve conflicts with task " + get<AbstractTask>(mission_decomposition[t1.first].content).id + ": " + get<AbstractTask>(mission_decomposition[t1.first].content).name; 
                        throw std::runtime_error(conflict_error);
                    } else if(found_task_decompositions[t2.first].size() == task_decompositions_number[t2.first]) {
                        std::string conflict_error = "Cannot solve conflicts with task " + get<AbstractTask>(mission_decomposition[t2.first].content).id + ": " + get<AbstractTask>(mission_decomposition[t2.first].content).name; 
                        throw std::runtime_error(conflict_error);
                    }

                    /*
                        Delete every mission decomposition that contain these conflicting task instances
                    */
                    vector<pair<vector<pair<int,ATNode>>,vector<ground_literal>>>::iterator mission_it;
                    for(mission_it = valid_mission_decompositions.begin(); mission_it != valid_mission_decompositions.end(); ) {
                        pair<bool,bool> found_instances = make_pair(false,false);
                        bool remove_decomposition = false;
                        for(pair<int,ATNode> t : mission_it->first) {
                            if(t.first == conflict.first.first) {
                                found_instances.first = true;
                            } else if(t.first == conflict.second.first) {
                                found_instances.second = true;
                            }

                            if(found_instances.first && found_instances.second) {
                                remove_decomposition = true;
                                break;
                            }
                        }

                        if(remove_decomposition) {
                            valid_mission_decompositions.erase(mission_it);
                        } else {
                            ++mission_it;
                        }
                    }
                }
            }
        }
    }
}