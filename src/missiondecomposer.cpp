#include "missiondecomposer.hpp"

#include <iostream>

#include <boost/foreach.hpp>

ATGraph build_at_graph(map<string,vector<AbstractTask>> at_instances, map<string,vector<vector<task>>> at_decomposition_paths, general_annot* gmannot, GMGraph gm, 
						vector<ground_literal> init, map<string, variant<pair<string,string>,pair<vector<string>,string>>> gm_vars_map, KnowledgeBase world_db,
							vector<SemanticMapping> semantic_mapping) {
	/*
		Generate Graph of all possible combinations of tasks

		REMEMBER: AT's contained in at_instances are mandatory, their decompositions are alternative

		-> We can add an ALT operator (or similar name) in order to define operators for this alternative decompositions we can have

		-> Main flow of things:
			- Go through the goal model runtime annotation (gmannot) and create nodes for the tasks, operators and decompositions for the ATGraph
				- Use AT instances to create the task nodes (each one of them will have a different id)
			- For each AT Node in the graph, we will add all of the possible decomposition paths
				- One thing to note is that we need to take into consideration the world state in order to define which are the valid decompositions
		
		-> A recursive implementation seems to be the best approach

		-> Let's not deal with the OPT or the FALLBACK case for the moment (29/11)
	*/

	ATGraph mission_decomposition;

	map<string, variant<string,vector<string>>> instantiated_vars;

	recursive_at_graph_build(mission_decomposition, init, at_instances, at_decomposition_paths, gmannot, -1, gm, false, gm_vars_map, world_db, semantic_mapping, instantiated_vars);

	return mission_decomposition;
}

void recursive_at_graph_build(ATGraph& mission_decomposition, vector<ground_literal> world_state, map<string,vector<AbstractTask>> at_instances, 
								map<string,vector<vector<task>>> at_decomposition_paths, general_annot* rannot, int parent, GMGraph gm, bool non_coop,
									map<string, variant<pair<string,string>,pair<vector<string>,string>>> gm_vars_map,KnowledgeBase world_db, 
										vector<SemanticMapping> semantic_mapping, map<string, variant<string,vector<string>>> instantiated_vars) {
										

	ATNode node;
	int node_id;
	if(rannot->type == OPERATOR || rannot->type == MEANSEND) {
		bool active_context = true;
		Context context;

		VertexData gm_node;

		bool is_forAll = false;
		pair<string,string> monitored_var;
		pair<string,string> controlled_var;

		if(rannot->related_goal != "") { 
			string n_id = rannot->related_goal;
			int gm_node_id = find_gm_node_by_id(n_id, gm);

			gm_node = gm[gm_node_id];
			if(gm_node.custom_props.find("CreationCondition") != gm_node.custom_props.end()) {
				context = get<Context>(gm_node.custom_props["CreationCondition"]);

				if(context.type == "condition") {
					active_context = check_context(context, world_state, semantic_mapping, instantiated_vars);
				}
			}
		} else {
			/*
				If we do not have a related goal this means that we have an operator generated by a forAll operator

				- variables are of the form <var_name,var_type>
			*/
			string n_id = rannot->children.at(0)->related_goal;
			int gm_node_id = find_gm_node_by_id(n_id, gm);

			gm_node = gm[gm_node_id];

			is_forAll = true;
			monitored_var = get<vector<pair<string,string>>>(gm_node.custom_props["Monitors"]).at(0);
			controlled_var = get<vector<pair<string,string>>>(gm_node.custom_props["Controls"]).at(0);
		}
		
		node.non_coop = rannot->non_coop;
		node.group = rannot->group;
		node.divisible = rannot->divisible;
		if(rannot->children.size() == 0 || rannot->type == MEANSEND) {
			node.node_type = GOALNODE;
		} else {
			node.node_type = OP;
		}
		node.content = rannot->content;

		node_id = boost::add_vertex(node, mission_decomposition);

		if(parent != -1) {
			ATEdge e;
			e.edge_type = NORMAL;
			e.source = parent;
			e.target = node_id;
			
			boost::add_edge(boost::vertex(parent, mission_decomposition), boost::vertex(node_id, mission_decomposition), e, mission_decomposition);
		}
		/*
			- Check if context is active
				- This requires a lot of thinking, how can we do that?
				- We cannot assume that the context will always be of the format [variable].[attribute], or can we?
					- Another valid way would be just a variable name for example
				- If it is an attribute, we have to check for the type of the variable
					- For now, the only valid types will be just one of the location types given in the configuration file
				- We will need to check a SemanticMapping that involves this attribute from this variable type
			- With this information, just search for all the ATASK type nodes and see if one of them leads to that context being active
		*/
		if(!active_context) {
			/*
				This will happen in two cases:
					- If we have a wrong model
					- If we have a parallel decomposition which is not completely parallel since we have a context dependency
			*/
			bool resolved_context = check_context_dependency(mission_decomposition, parent, node_id, context, rannot, world_state, instantiated_vars, at_decomposition_paths, semantic_mapping);
			
			if(!resolved_context) {
				cout << "COULD NOT RESOLVE CONTEXT FOR NODE: " << gm_node.text << endl; 
				exit(1);
			}
		}

		if(!non_coop) {
			non_coop = node.non_coop;
		}
		/*
			If the node is non cooperative (non-group or non-divisible group) we create non coop links between AT's that are children of it.

			-> We just create these links at the last child AT
		*/
		unsigned int child_index = 0;
		if(is_forAll) {
			/*
				- Controlled variable in forAll needs to be of container type
				- Monitored variable in forAll needs to be of value type
			*/
			int value_index = 0;
			for(general_annot* child : rannot->children) {
				pair<vector<string>,string> var_map = get<pair<vector<string>,string>>(gm_vars_map[monitored_var.first]);
				instantiated_vars[controlled_var.first] = var_map.first.at(value_index);
				value_index++;

				if(child_index < rannot->children.size()-1) {
					recursive_at_graph_build(mission_decomposition, world_state, at_instances, at_decomposition_paths, child, node_id, gm, false, gm_vars_map, world_db, semantic_mapping, instantiated_vars);
				} else {
					recursive_at_graph_build(mission_decomposition, world_state, at_instances, at_decomposition_paths, child, node_id, gm, non_coop, gm_vars_map, world_db, semantic_mapping, instantiated_vars);
				}
				child_index++;
			}
		} else {
			for(general_annot* child : rannot->children) {
				if(child_index < rannot->children.size()-1) {
					recursive_at_graph_build(mission_decomposition, world_state, at_instances, at_decomposition_paths, child, node_id, gm, false, gm_vars_map, world_db, semantic_mapping, instantiated_vars);
				} else {
					recursive_at_graph_build(mission_decomposition, world_state, at_instances, at_decomposition_paths, child, node_id, gm, non_coop, gm_vars_map, world_db, semantic_mapping, instantiated_vars);
				}
				child_index++;
			}
		}
	} else if(rannot->type == TASK) {
		node.non_coop = true;
		node.node_type = ATASK;
		
		//Find AT instance that corresponds to this node and put it in the content
		bool found_at = false;
		map<string,vector<AbstractTask>>::iterator at_inst_it;
		for(at_inst_it = at_instances.begin();at_inst_it != at_instances.end();++at_inst_it) {
			string rannot_id, at_id;
			rannot_id = rannot->content.substr(0,rannot->content.find("_"));
			at_id = at_inst_it->second.at(0).id.substr(0,at_inst_it->second.at(0).id.find("_"));
			
			if(at_id == rannot_id) { //If we are dealing with the same task
				for(AbstractTask at : at_inst_it->second) {
					if(at.id == rannot->content) {
						node.content = at;
						found_at = true;
						break;
					}
				}
			}

			if(found_at) break;
		}

		node_id = boost::add_vertex(node, mission_decomposition);

		ATEdge e;
		e.edge_type = NORMAL;
		e.source = parent;
		e.target = node_id;
		
		boost::add_edge(boost::vertex(parent, mission_decomposition), boost::vertex(node_id, mission_decomposition), e, mission_decomposition);

		if(non_coop) {
			create_non_coop_edges(mission_decomposition,node_id);
		}

		//Now we need to insert valid decompositions
		map<string,vector<vector<task>>> valid_decomposition_paths;

		AbstractTask at = get<AbstractTask>(node.content);
		for(vector<task> path : at_decomposition_paths[at.name]) {
			bool is_valid = check_path_validity(path, world_state, at);

			if(is_valid) {
				valid_decomposition_paths[at.name].push_back(path);
			}
		}

		int path_id = 1;
		for(vector<task> path : valid_decomposition_paths[at.name]) {
			ATNode path_node;
			path_node.node_type = DECOMPOSITION;
			path_node.non_coop = true;

			Decomposition d;
			d.id = at.id + "|" + to_string(path_id);
			d.path = path;
			d.at = at;
			instantiate_decomposition_predicates(at,d,gm_vars_map);

			path_id++;

			path_node.content = d;

			int dnode_id = boost::add_vertex(path_node, mission_decomposition);

			ATEdge d_edge;
			e.edge_type = NORMAL;
			e.source = node_id;
			e.target = dnode_id;

			boost::add_edge(boost::vertex(node_id, mission_decomposition), boost::vertex(dnode_id, mission_decomposition), d_edge, mission_decomposition);
		}
	}
}

bool check_path_validity(vector<task> path, vector<ground_literal> world_state, AbstractTask at) {
	bool valid_path = true;
	for(task t : path) {
		bool prec_satistfied = true;
		for(literal prec : t.prec) {
			/*
				Check if predicate involves an instantiated variable that belongs to the variable mapping of the AT
			*/
			bool instantiated_prec = true;
			vector<pair<string,string>> arg_map;
			for(string arg : prec.arguments) {
				bool found_arg = false;
				string mapped_var;
				for(pair<string,string> var_map : at.variable_mapping) {
					if(arg == var_map.second) {
						found_arg = true;
						mapped_var = var_map.first;
						break;
					}
				}

				if(!found_arg) {
					instantiated_prec = false;
					break;
				}

				arg_map.push_back(make_pair(arg,mapped_var));
			}

			if(instantiated_prec) {
				ground_literal inst_prec;
				inst_prec.positive = prec.positive;
				inst_prec.predicate = prec.predicate;
				for(pair<string,string> arg_inst : arg_map) {
					inst_prec.args.push_back(arg_inst.second);
				}

				for(ground_literal state : world_state) {
					if(state.predicate == inst_prec.predicate) {
						bool equal_args = true;
						for(unsigned int arg_index = 0;arg_index < state.args.size();arg_index++) {
							if(state.args.at(arg_index) != inst_prec.args.at(arg_index)) {
								equal_args = false;
								break;
							}
						}

						if(equal_args && (prec.positive != state.positive)) {
							prec_satistfied = false;
							break;
						}
					}
				}

				if(!prec_satistfied) {
					valid_path = false;
					break;
				}
			}
		}

		if(!valid_path) break;
	}

	return valid_path;
}

void instantiate_decomposition_predicates(AbstractTask at, Decomposition& d, map<string, variant<pair<string,string>,pair<vector<string>,string>>> gm_vars_map) {
	int task_counter = 1,task_number;

	task_number = d.path.size();

	vector<variant<ground_literal,literal>> combined_effects;

	for(task t : d.path) {
		if(task_counter == 1) { //First task defines preconditions
			for(literal prec : t.prec) {
				bool can_ground = true;
				for(string arg : prec.arguments) {
					bool found_arg = false;
					for(pair<string,string> var_map : at.variable_mapping) {
						if(arg == var_map.second) {
							found_arg = true;
							break;
						}
					}

					if(!found_arg) {
						can_ground = false;
						break;
					}
				}

				if(can_ground) {
					ground_literal inst_prec;
					inst_prec.positive = prec.positive;
					inst_prec.predicate = prec.predicate;

					for(string arg : prec.arguments) {
						for(pair<string,string> var_map : at.variable_mapping) {
							if(arg == var_map.second) {
								inst_prec.args.push_back(var_map.first);
							}
						}
					}

					d.prec.push_back(inst_prec);
				} else {
					d.prec.push_back(prec);
				}
			}
		}

		//Here we apply the effects
		for(literal eff : t.eff) {
			bool can_ground = true;
			for(string arg : eff.arguments) {
				bool found_arg = false;
				for(pair<string,string> var_map : at.variable_mapping) {
					if(arg == var_map.second) {
						found_arg = true;
						break;
					}
				}

				if(!found_arg) {
					can_ground = false;
					break;
				}
			}
		
			if(can_ground) {
				ground_literal inst_eff;
				inst_eff.positive = eff.positive;
				inst_eff.predicate = eff.predicate;

				for(string arg : eff.arguments) {
					for(pair<string,string> var_map : at.variable_mapping) {
						if(arg == var_map.second) {
							inst_eff.args.push_back(var_map.first);
						}
					}
				}

				bool applied_effect = false;
				for(unsigned int i = 0;i < combined_effects.size();i++) {
					if(holds_alternative<ground_literal>(combined_effects.at(i))) {
						ground_literal ceff = get<ground_literal>(combined_effects.at(i));
						if(inst_eff.predicate == ceff.predicate) {
							bool equal_args = true;
							int index = 0;
							for(auto arg : inst_eff.args) {
								if(arg != ceff.args.at(index)) {
									equal_args = false;
									break;
								}
								index++;
							}

							if(equal_args) {
								if(inst_eff.positive != ceff.positive) {
									combined_effects.at(i) = inst_eff;
								}
								applied_effect = true;
								break;
							}
						}
					}
				}

				if(!applied_effect) {
					combined_effects.push_back(inst_eff);
				}
			} else {
				bool applied_effect = false;
				for(unsigned int i = 0;i < combined_effects.size();i++) {
					if(holds_alternative<literal>(combined_effects.at(i))) {
						literal ceff = get<literal>(combined_effects.at(i));
						if(eff.predicate == ceff.predicate) {
							bool equal_args = true;
							int index = 0;
							for(auto arg : eff.arguments) {
								if(arg != ceff.arguments.at(index)) {
									equal_args = false;
									break;
								}
								index++;
							}

							if(equal_args) {
								if(eff.positive != ceff.positive) {
									combined_effects.at(i) = eff;
								}
								applied_effect = true;
								break;
							}
						}
					}
				}

				if(!applied_effect) {
					combined_effects.push_back(eff);
				}
			}
		}

		if(task_counter == task_number) { //Last task defines effects
			d.eff = combined_effects;
		}

		task_counter++;
	}
}

pair<bool,pair<string,predicate_definition>> get_pred_from_context(Context context, vector<SemanticMapping> semantic_mapping) {
	predicate_definition pred;
	string var;
	bool positive = true;

	if(context.type == "condition") {
		string condition = context.condition;

		/*
			For now we will accept only the attribute format

			-> This means that we will have as the context something in the form [variable].[attribute]
		*/
		string attr;

		size_t cond_sep = condition.find('.');

		var = condition.substr(0,cond_sep);
		attr = condition.substr(cond_sep+1,condition.size()-cond_sep-1);

		/*
			Check attribute value and return it

			-> We need a way to check the variable type here
		*/
		for(SemanticMapping map : semantic_mapping) {
			if(map.get_mapping_type() == "attribute") {
				if(get<string>(map.get_prop("name")) == attr) {
					pred = get<predicate_definition>(map.get_prop("map"));
					break;
				}
			}
		}

		if(condition.find("!") != std::string::npos || condition.find("not") != std::string::npos) {
			positive = false;
		}
	}

	return make_pair(positive,make_pair(var,pred));
}

bool check_context(Context context, vector<ground_literal> world_state, vector<SemanticMapping> semantic_mapping,
					map<string, variant<string,vector<string>>> instantiated_vars) {
	pair<bool,pair<string,predicate_definition>> var_and_pred = get_pred_from_context(context, semantic_mapping);

	var_and_pred.second.first = get<string>(instantiated_vars[var_and_pred.second.first]);

	bool is_active = false;
	for(ground_literal state : world_state) {
		if(state.predicate == var_and_pred.second.second.name) {
			if(state.args.size() == 1) { //We have an attribute of one variable only
				if(state.args.at(0) == var_and_pred.second.first) {
					if(state.positive == var_and_pred.first) {
						is_active = true;
					}
				}
			}
		}
	}

	return is_active;
}

bool check_context_dependency(ATGraph& mission_decomposition, int parent_node, int current_node, Context context, general_annot* rannot, vector<ground_literal> world_state,
								map<string, variant<string,vector<string>>> instantiated_vars, map<string,vector<vector<task>>> at_decomposition_paths,
									vector<SemanticMapping> semantic_mapping) {
	auto indexmap = boost::get(boost::vertex_index, mission_decomposition);
    auto colormap = boost::make_vector_property_map<boost::default_color_type>(indexmap);

    DFSATVisitor vis;
    boost::depth_first_search(mission_decomposition, vis, colormap, parent_node);

    std::vector<int> vctr = vis.GetVector();

	/*
		Go through the goal model and verify effects of abstract tasks that are children of it

		-> If any effects corresponds to the given context we link this node with the node that has the context
	*/

	pair<bool,pair<string,predicate_definition>> var_and_pred =  get_pred_from_context(context, semantic_mapping);

	bool found_at = false;
	for(int v : vctr) {
		//Go through the valid paths of an AbstractTask and create ContextDependency links
		if(mission_decomposition[v].node_type == ATASK) {
			AbstractTask at = get<AbstractTask>(mission_decomposition[v].content);

			/*
				-> When we have them we need to verify the effects related to the variable in the var_map
				-> If at the end of one decomposition we have the effect that makes the context valid, we need to make a
				ContextDependency link between this task node and the one related to the context

				-> For now we are only considering effects and not conditional effects
			*/
			vector<int> decompositions;

			ATGraph::edge_iterator init, end;

			for(boost::tie(init,end) = edges(mission_decomposition);init != end;++init) {
				int source, target;

				source = (*init).m_source;
				target = (*init).m_target;
				if(source == v && mission_decomposition[target].node_type == DECOMPOSITION) {
					decompositions.push_back(target);
				}
			}
			
			for(int d_id : decompositions) {
				bool context_satisfied = false;
				vector<task> path = get<Decomposition>(mission_decomposition[d_id].content).path;
				vector<ground_literal> world_state_copy = world_state;
				for(task t : path) {
					for(literal eff : t.eff) {
						bool instantiated_eff = true;
						vector<pair<string,string>> arg_map;
						for(string arg : eff.arguments) {
							bool found_arg = false;
							string mapped_var;
							for(pair<string,string> var_map : at.variable_mapping) {
								if(arg == var_map.second) {
									found_arg = true;
									mapped_var = var_map.first;
									break;
								}
							}

							if(!found_arg) {
								instantiated_eff = false;
								break;
							}

							arg_map.push_back(make_pair(arg,mapped_var));
						}

						if(instantiated_eff) {
							ground_literal inst_eff;
							inst_eff.positive = eff.positive;
							inst_eff.predicate = eff.predicate;
							for(pair<string,string> arg_inst : arg_map) {
								inst_eff.args.push_back(arg_inst.second);
							}

							bool effect_applied = false;
							for(ground_literal& state : world_state_copy) {
								if(state.predicate == inst_eff.predicate) {
									bool equal_args = true;
									for(unsigned int arg_index = 0;arg_index < state.args.size();arg_index++) {
										if(state.args.at(arg_index) != inst_eff.args.at(arg_index)) {
											equal_args = false;
											break;
										}
									}

									if(equal_args && (eff.positive != state.positive)) {
										state.positive = eff.positive;
										effect_applied = true;
										break;
									}
								}
							}

							if(!effect_applied) {
								world_state_copy.push_back(inst_eff);
							}
						}
					}
				}

				/*
					Build a ground literal from the predicate definition
				*/
				ground_literal context_pred;
				context_pred.predicate = var_and_pred.second.second.name;
				string var_name = get<string>(instantiated_vars[var_and_pred.second.first]);
				context_pred.args.push_back(var_name);
				context_pred.positive = var_and_pred.first;

				for(ground_literal state : world_state_copy) {
					if(state.predicate == context_pred.predicate) {
						bool equal_args = true;
						for(unsigned int arg_index = 0;arg_index < state.args.size();arg_index++) {
							if(state.args.at(arg_index) != context_pred.args.at(arg_index)) {
								equal_args = false;
								break;
							}
						}

						if(equal_args && (context_pred.positive == state.positive)) {
							context_satisfied = true;
							break;
						}
					}
				}

				//If context is satisfied, insert a ContextDependency edge
				if(context_satisfied) {
					ATEdge e;
					e.edge_type = CDEPEND;
					e.source = d_id;
					e.target = current_node;
				
					boost::add_edge(boost::vertex(d_id, mission_decomposition), boost::vertex(current_node, mission_decomposition), e, mission_decomposition);

					cout << "Context satisfied with task " << get<Decomposition>(mission_decomposition[d_id].content).id << ": " << at.name << endl;

					//For now we create the dependency between the first AT that satisfies its context
					found_at = true;
				}
			}

			if(found_at) break;
		}
	}

	return found_at;
}

vector<pair<int,ATNode>> find_decompositions(ATGraph mission_decomposition, int node_id) {
	/*
		Search for all decompositions for a specific node and return them
	*/

	vector<pair<int,ATNode>> node_decompositions;
	if(mission_decomposition[node_id].node_type == ATASK) {
		ATGraph::out_edge_iterator ei, ei_end;

		//Insert only the nodes which are linked by outer edges of type NORMAL
		for(boost::tie(ei,ei_end) = out_edges(node_id,mission_decomposition);ei != ei_end;++ei) {
			auto target = boost::target(*ei,mission_decomposition);

			if(mission_decomposition[target].node_type == DECOMPOSITION) {
				node_decompositions.push_back(make_pair(target,mission_decomposition[target]));		
			}
		}
	} else {
		string node_type;

		if(mission_decomposition[node_id].node_type == OP) {
			node_type = "Operation";
		} else if(mission_decomposition[node_id].node_type == DECOMPOSITION) {
			node_type = "Decomposition";
		} else if(mission_decomposition[node_id].node_type == GOALNODE) {
			node_type = "Goal";
		} else {
			node_type = "Unknown";
		}

		cout << "WARNING: Node of type " <<  node_type << " does not have decompositions." << endl;
	}

	return node_decompositions;
}

void create_non_coop_edges(ATGraph& mission_decomposition, int node_id) {
	int non_coop_parent_id = -1;
	int current_node = node_id;

	bool is_group = false;
	bool is_divisible = false;
	while(non_coop_parent_id == -1) {
		ATGraph::in_edge_iterator in_begin, in_end;

        //Find out if the parent has non cooperative set to True
        for(boost::tie(in_begin,in_end) = in_edges(current_node,mission_decomposition);in_begin != in_end;++in_begin) {
            auto source = boost::source(*in_begin,mission_decomposition);
            auto target = boost::target(*in_begin,mission_decomposition);
            auto edge = boost::edge(source,target,mission_decomposition);

			if(mission_decomposition[edge.first].edge_type == NORMAL) {
				if(mission_decomposition[source].non_coop) {
					non_coop_parent_id = source;
					is_group = mission_decomposition[source].group;
					is_divisible = mission_decomposition[source].divisible;
					break;
				} else {
					current_node = source;
				}
			}
		}
	}

	set<int> non_coop_task_ids;
	find_non_coop_task_ids(mission_decomposition, non_coop_parent_id, non_coop_task_ids);

	/*
		Do we really need to create Edges in both ways?
	*/
	for(int t1 : non_coop_task_ids) {
		for(int t2 : non_coop_task_ids) {
			if(t1 != t2) {
				ATEdge e1;
				e1.edge_type = NONCOOP;
				e1.source = t1;
				e1.target = t2;
				e1.group = is_group;
				e1.divisible = is_divisible;
				
				bool edge_exists = boost::edge(t1,t2,mission_decomposition).second;
				if(!edge_exists) {
					boost::add_edge(boost::vertex(t1, mission_decomposition), boost::vertex(t2, mission_decomposition), e1, mission_decomposition);
				}

				ATEdge e2;
				e2.edge_type = NONCOOP;
				e2.source = t2;
				e2.target = t1;
				e2.group = is_group;
				e2.divisible = is_divisible;

				edge_exists = boost::edge(t2,t1,mission_decomposition).second;
				if(!edge_exists) {
					boost::add_edge(boost::vertex(t2, mission_decomposition), boost::vertex(t1, mission_decomposition), e2, mission_decomposition);
				}
			}
		}
	}
}

void find_non_coop_task_ids(ATGraph mission_decomposition, int node_id, set<int>& task_ids) {
	if(mission_decomposition[node_id].node_type != ATASK) {
		ATGraph::out_edge_iterator ei, ei_end;

		for(boost::tie(ei,ei_end) = out_edges(node_id,mission_decomposition);ei != ei_end;++ei) {
			auto source = boost::source(*ei,mission_decomposition);
			auto target = boost::target(*ei,mission_decomposition);
			auto edge = boost::edge(source,target,mission_decomposition);

			if(mission_decomposition[edge.first].edge_type == NORMAL) {
				find_non_coop_task_ids(mission_decomposition,target,task_ids);
			}
		}
	} else {
		task_ids.insert(node_id);
	}
}

bool can_unite_decompositions(Decomposition d1, Decomposition d2, bool non_coop_nodes) {
	/*
		Here we check if the effects of one decomposition affect the preconditions of another decomposition

		-> Predicates not present in the effects are considered to be true from the beginning
		-> If tasks are non_coop we cannot assume nothing about the non_instantiated predicates
			- If they are, we can assume everything robot related refers to the same constant(s)
		
		-> The way we are performing this right now does not seem to be right
			- IDEA: Transform the initial state and then confront it with the preconditions, if we don't have conflicts we are ok
	*/
	bool can_unite = true;

	vector<variant<ground_literal,literal>> d1_eff = d1.eff;
	vector<variant<ground_literal,literal>> d2_prec = d2.prec;

	if(non_coop_nodes) {
		vector<variant<ground_literal,literal>> initial_state = d2_prec;

		for(auto& state : initial_state) {
			if(holds_alternative<literal>(state)) {
				literal s = get<literal>(state);
				int found_eff = -1;

				for(unsigned int i = 0;i < d1_eff.size();i++) {
					if(holds_alternative<literal>(d1_eff.at(i))) {
						literal eff = get<literal>(d1_eff.at(i));
						if(eff.predicate == s.predicate) {
							bool equal_args = true;

							int arg_index = 0;
							for(string arg : s.arguments) {
								if(arg != eff.arguments.at(arg_index)) {
									equal_args = false;
									break;
								}
								arg_index++;
							}

							if(equal_args) {
								if(s.positive == eff.positive) {
									found_eff = i;
									break;
								} else {
									s.positive = eff.positive;
									found_eff = i;
									state = s;
									break;
								}
							}
						}
					}
				}

				if(found_eff != -1) {
					d1_eff.erase(d1_eff.begin()+found_eff);
				}
			} else {
				ground_literal s = get<ground_literal>(state);
				int found_eff = -1;
				for(unsigned int i = 0;i < d1_eff.size();i++) {
					if(holds_alternative<ground_literal>(d1_eff.at(i))) {
						ground_literal eff = get<ground_literal>(d1_eff.at(i));
						if(eff.predicate == s.predicate) {
							bool equal_args = true;

							int arg_index = 0;
							for(string arg : s.args) {
								if(arg != eff.args.at(arg_index)) {
									equal_args = false;
									break;
								}
								arg_index++;
							}

							if(equal_args) {
								if(s.positive == eff.positive) {
									found_eff = i;
									break;
								} else {
									found_eff = i;
									s.positive = eff.positive;
									state = s;
									break;
								}
							}
						}
					}
				}

				if(found_eff != -1) {
					d1_eff.erase(d1_eff.begin()+found_eff);
				}
			}
		}

		int index = 0;
		for(auto prec : d2_prec) {
			if(holds_alternative<literal>(prec)) {
				literal p = get<literal>(prec);
				literal p1 = get<literal>(initial_state.at(index));

				if(p.positive != p1.positive) {
					can_unite = false;
					break;
				}
			} else {
				ground_literal p = get<ground_literal>(prec);
				ground_literal p1 = get<ground_literal>(initial_state.at(index));

				if(p.positive != p1.positive) {
					can_unite = false;
					break;
				}
			}

			index++;
		}
	} else {
		vector<ground_literal> initial_state;
		for(auto prec : d2_prec) {
			if(holds_alternative<ground_literal>(prec)) {
				initial_state.push_back(get<ground_literal>(prec));
			}
		}

		for(ground_literal& state : initial_state) {
			int found_eff = -1;
			for(unsigned int i = 0;i < d1_eff.size();i++) {
				if(holds_alternative<ground_literal>(d1_eff.at(i))) {
					ground_literal eff = get<ground_literal>(d1_eff.at(i));
					if(eff.predicate == state.predicate) {
						bool equal_args = true;

						int arg_index = 0;
						for(string arg : state.args) {
							if(arg != eff.args.at(arg_index)) {
								equal_args = false;
								break;
							}
							arg_index++;
						}

						if(equal_args) {
							if(state.positive == eff.positive) {
								found_eff = i;
								break;
							} else {
								state.positive = eff.positive;
								found_eff = i;
							}
						}
					}
				}
			}

			if(found_eff != -1) {
				d1_eff.erase(d1_eff.begin()+found_eff);
			}
		}

		int index = 0;
		for(auto prec : d2_prec) {
			if(holds_alternative<ground_literal>(prec)) {
				ground_literal p = get<ground_literal>(prec);
				if(p.positive != initial_state.at(index).positive) {
					can_unite = false;
					break;
				}

				index++;
			}
		}
	}

	return can_unite;
}

void print_mission_decomposition(ATGraph mission_decomposition) {
	ATGraph::vertex_iterator i, end;
	ATGraph::adjacency_iterator ai, a_end;

	for(boost::tie(i,end) = vertices(mission_decomposition); i != end; ++i) {
		ATNode node = mission_decomposition[*i];
		if(holds_alternative<AbstractTask>(node.content)) {
			std::cout << get<AbstractTask>(node.content).id << " --> ";
		} else if(holds_alternative<string>(node.content)) {
			std::cout << get<string>(node.content) << " --> ";
		} else {
			std::cout << get<Decomposition>(node.content).id << " --> ";	
		}

		for(boost::tie(ai,a_end) = adjacent_vertices(*i,mission_decomposition); ai != a_end;++ai) {
			ATNode a_node = mission_decomposition[*ai];
			if(holds_alternative<AbstractTask>(a_node.content)) {
				std::cout << get<AbstractTask>(a_node.content).id << " ";
			} else if(holds_alternative<string>(a_node.content)) {
				std::cout << get<string>(a_node.content) << " ";
			} else {
				std::cout << get<Decomposition>(a_node.content).id << " ";
			}
		}	
		std::cout << std::endl;
	}

	std::cout << std::endl;
}