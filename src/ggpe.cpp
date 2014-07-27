#include "ggpe.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/timer.hpp>
#include <boost/functional/hash.hpp>
#include <boost/bimap/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <Yap/YapInterface.h>
#include <glog/logging.h>

#include "sexpr_parser.hpp"
#include "file_utils.hpp"
#include "yap_engine.hpp"
#include "gdlcc_engine.hpp"
#include "prettyprint.hpp"

namespace ggpe {

template <class K, class V>
using UnorderedBimap = boost::bimaps::bimap<boost::bimaps::unordered_set_of<K>, boost::bimaps::unordered_set_of<V>>;
using AtomAndString = UnorderedBimap<Atom, std::string>::value_type;
UnorderedBimap<Atom, std::string> atom_to_string;
std::string game_name;
std::vector<Atom> roles;
std::vector<int> role_indices;
std::unordered_map<Atom, int> atom_to_role_index;
std::unordered_map<Atom, int> atom_to_functor_arity;
std::unordered_map<Atom, int> atom_to_goal_values;
std::vector<Tuple> initial_facts;
std::vector<Tuple> possible_facts;
std::vector<std::vector<Tuple>> possible_actions;
std::unordered_map<Atom, std::unordered_map<Atom, int>> atom_to_ordered_domain;
std::unordered_set<Atom> step_counter_atoms;
std::unordered_map<AtomPair, std::vector<std::pair<Atom, std::pair<int, int>>>, boost::hash<AtomPair>> fact_action_connections;
std::unordered_map<Atom, std::unordered_map<int, Atom>> fact_ordered_args;
std::unordered_map<Atom, std::unordered_map<int, Atom>> action_ordered_args;
std::string game_kif = "";
bool game_enables_tabling = false;
EngineBackend engine_backend;
bool is_yap_engine_initialized = false;
bool is_gdlcc_engine_initialized = false;
std::vector<std::vector<FactSet>> win_conditions;

//template <class Iterator>
//std::string TupleToString(const Iterator& begin, const Iterator& end) {
//  assert(std::distance(begin, end) > 0);
//  if (std::distance(begin, end) == 1) {
//    // Atom
//    return std::string(AtomToString(*begin));
//  }
//  // Compound term
//  std::ostringstream o;
//  o << '(';
//  o << AtomToString(*begin) << ' ';
//  for (auto i = begin + 1; i != end; ++i) {
//    const auto atom = *i;
////    o << AtomToString(atom);
//    if (atom_to_functor_arity.count(atom)) {
//      // Functor atom
//      const auto arity = atom_to_functor_arity[atom];
//      o << TupleToString(i, i + arity);
//      i += arity - 1;
//    } else {
//      // Non-functor atom
//      o << AtomToString(atom);
//    }
//    if (i != end - 1) {
//      o << ' ';
//    }
//  }
//  o << ')';
//  return o.str();
//}

const std::string& AtomToString(const Atom atom) {
  return atom_to_string.left.at(atom);
}

Atom StringToAtom(const std::string& atom_str) {
  return atom_to_string.right.at(atom_str);
}

std::string TupleToString(const Tuple& tuple) {
  if (tuple.size() == 1) {
    return AtomToString(tuple.front());
  } else {
    std::ostringstream o;
    o << '(';
    for (auto it = tuple.begin(); it != tuple.end(); ++it) {
      o << AtomToString(*it);
      if (it != tuple.end() - 1) {
        o << ' ';
      }
    }
    o << ')';
    return o.str();
  }
//  return TupleToString(tuple.begin(), tuple.end());
}

Tuple NodeToTuple(const sexpr_parser::TreeNode& node) {
  if (node.IsLeaf()) {
    const auto atom = StringToAtom(node.GetValue());
    return Tuple({atom});
  } else {
    assert(node.GetChildren().size() >= 2);
    assert(node.GetChildren().front().IsLeaf());
    Tuple tuple;
    tuple.reserve(node.GetChildren().size());
    tuple.push_back(StringToAtom(node.GetChildren().front().GetValue()));
    const auto& children = node.GetChildren();
    for (auto i = children.begin() + 1; i != children.end(); ++i) {
      const auto arg_tuple = NodeToTuple(*i);
      tuple.insert(tuple.end(), arg_tuple.begin(), arg_tuple.end());
    }
    return tuple;
  }
}

Tuple StringToTuple(const std::string& str) {
  const auto nodes = sexpr_parser::ParseKIF(str);
  assert(nodes.size() == 1);
  const auto node = nodes.front();
  return NodeToTuple(node);
}

bool IsGDLCCEngineValid() {
  assert(is_yap_engine_initialized);
  auto yap_state = yap::CreateInitialState();
  auto gdlcc_state = gdlcc::CreateInitialState();
  while (!yap_state->IsTerminal()) {
    if (gdlcc_state->IsTerminal()) {
      std::cout << "YapState is not terminal, but CppState is terminal." << std::endl;
      std::cout << "YapState:\n" << yap_state->ToString();
      std::cout << "CppState:\n" << gdlcc_state->ToString();
      return false;
    }
    auto yap_facts = yap_state->GetFacts();
    auto gdlcc_facts = gdlcc_state->GetFacts();
    if (yap_facts.size() != gdlcc_facts.size()) {
      std::cout << "The number of facts in a state differs for YapState and CppState." << std::endl;
      std::cout << "YapState:" << yap_state->GetFacts().size() << std::endl << yap_state->ToString();
      std::cout << "CppState:" << gdlcc_state->GetFacts().size() << std::endl << gdlcc_state->ToString();
      return false;
    }
    std::sort(yap_facts.begin(), yap_facts.end());
    std::sort(gdlcc_facts.begin(), gdlcc_facts.end());
    if (yap_facts != gdlcc_facts) {
      std::cout << "The facts in a state differ for YapState and CppState." << std::endl;
      std::cout << "yap_facts" << yap_facts << std::endl;
      std::cout << "gdlcc_facts" << gdlcc_facts << std::endl;
      std::cout << "YapState:" << std::endl;
      std::cout << yap_state->ToString();
      std::cout << "CppState:" << std::endl;
      std::cout << gdlcc_state->ToString();
      return false;
    }
    if (yap_state->GetLegalActions().size() != gdlcc_state->GetLegalActions().size()) {
      std::cout << "The number of legal actions in a state differs for YapState and CppState." << std::endl;
      std::cout << "YapState:" << yap_state->GetLegalActions().size() << std::endl << yap_state->ToString();
      std::cout << "CppState:" << gdlcc_state->GetLegalActions().size() << std::endl << gdlcc_state->ToString();
      return false;
    }
    JointAction joint_action;
    for (auto role_idx : GetRoleIndices()) {
      auto yap_actions = yap_state->GetLegalActions()[role_idx];
      auto gdlcc_actions = gdlcc_state->GetLegalActions()[role_idx];
      if (yap_actions.size() != gdlcc_actions.size()) {
        std::cout << "The number of legal actions for " << role_idx << " in a state differs for YapState and CppState." << std::endl;
        std::cout << "YapState:" << yap_state->GetLegalActions()[role_idx].size() << std::endl << yap_state->ToString();
        std::cout << "CppState:" << gdlcc_state->GetLegalActions()[role_idx].size() << std::endl << gdlcc_state->ToString();
        return false;
      }
      std::sort(yap_actions.begin(), yap_actions.end());
      std::sort(gdlcc_actions.begin(), gdlcc_actions.end());
      if (yap_actions != gdlcc_actions) {
        std::cout << "The legal actions for " << role_idx << " in a state differ for YapState and CppState." << std::endl;
        std::cout << "YapState:\n" << yap_state->ToString();
        std::cout << "CppState:\n" << gdlcc_state->ToString();
        return false;
      }
      joint_action.push_back(yap_actions.front());
    }
    yap_state = yap_state->GetNextState(joint_action);
    gdlcc_state = gdlcc_state->GetNextState(joint_action);
  }
  if (!yap_state->IsTerminal()) {
    std::cout << "YapState is terminal, but CppState is not terminal." << std::endl;
    std::cout << "YapState:\n" << yap_state->ToString();
    std::cout << "CppState:\n" << gdlcc_state->ToString();
    return false;
  }
  if (yap_state->GetGoals() != gdlcc_state->GetGoals()) {
    std::cout << "The goal value in a state differs for YapState and CppState." << std::endl;
    std::cout << "YapState:\n" << yap_state->ToString();
    std::cout << "CppState:\n" << gdlcc_state->ToString();
    return false;
  }
  std::cout << "GDLCC engine is valid." << std::endl;
  return true;
}

void Initialize(
    const std::string& kif,
    const std::string& name,
    const EngineBackend backend,
    const bool enables_tabling) {
  assert(!kif.empty());
  assert(!name.empty());
  game_name = name;
  if (game_kif == kif &&
      engine_backend == backend &&
      game_enables_tabling == enables_tabling) {
    // Nothing to do
    return;
  }
  game_kif = kif;
  engine_backend = backend;
  game_enables_tabling = enables_tabling;
  is_yap_engine_initialized = false;
  is_gdlcc_engine_initialized = false;
#ifndef GGPE_SINGLE_THREAD
  std::cout << "Thread-safe mode." << std::endl;
#else
  std::cout << "Single-thread mode." << std::endl;
#endif

  // Initialize yap engine
  yap::InitializeYapEngine(kif, name, enables_tabling);
  is_yap_engine_initialized = true;
  std::cout << "Initialized yap engine." << std::endl;

  // Initialize gdlcc engine
  if (backend == EngineBackend::GDLCC) {
    if (gdlcc::InitializeGDLCCEngineOrFalse(kif, name, true) &&
        IsGDLCCEngineValid()) {
      std::cout << "Initialized gdlcc engine." << std::endl;
      is_gdlcc_engine_initialized = true;
    } else {
      std::cout << "Failed to initialize gdlcc engine." << std::endl;
    }
  }
}

void InitializeFromFile(
    const std::string& kif_filename,
    const EngineBackend backend,
    const bool enables_tabling) {
  boost::filesystem::path path(kif_filename);
  Initialize(
      file_utils::LoadStringFromFile(kif_filename),
      path.stem().string(),
      backend,
      enables_tabling);
}

const std::vector<Tuple>& GetPossibleFacts() {
  return possible_facts;
}

const std::vector<std::vector<Tuple>>& GetPossibleActions() {
  return possible_actions;
}

int GetRoleCount() {
  return roles.size();
}

const std::vector<int>& GetRoleIndices() {
  return role_indices;
}

bool IsValidRoleIndex(const int role_idx) {
  return std::find(role_indices.begin(), role_indices.end(), role_idx) != role_indices.end();
}

std::string RoleIndexToString(const int role_index) {
  return AtomToString(roles[role_index]);
}

int StringToRoleIndex(const std::string& role_str) {
  return atom_to_role_index.at(StringToAtom(role_str));
}


std::string JointActionToString(const JointAction& joint_action) {
  assert(joint_action.size() == roles.size());
  std::ostringstream o;
  o << '(';
  for (auto i = joint_action.begin(); i != joint_action.end(); ++i) {
    if (i != joint_action.begin()) {
      o << ' ';
    }
    o << TupleToString(*i);
  }
  o << ')';
  return o.str();
}

const std::unordered_set<Atom>& GetStepCounters() {
  return step_counter_atoms;
}

const std::unordered_map<AtomPair, std::vector<std::pair<Atom, std::pair<int, int>>>, boost::hash<AtomPair>>& GetFactActionConnections() {
  return fact_action_connections;
}

const std::unordered_map<Atom, std::unordered_map<Atom, int>>& GetOrderedDomains() {
  return atom_to_ordered_domain;
}

void InitializeTicTacToe(const EngineBackend backend) {
  const std::string tictactoe_kif = R"(
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Tictactoe
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Components
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

    (role white)
    (role black)

    (<= (base (cell ?m ?n x)) (index ?m) (index ?n))
    (<= (base (cell ?m ?n o)) (index ?m) (index ?n))
    (<= (base (cell ?m ?n b)) (index ?m) (index ?n))
    (base (control white))
    (base (control black))

    (<= (input ?r (mark ?m ?n)) (role ?r) (index ?m) (index ?n))
    (<= (input ?r noop) (role ?r))

    (index 1)
    (index 2)
    (index 3)
  
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; init
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

    (init (cell 1 1 b))
    (init (cell 1 2 b))
    (init (cell 1 3 b))
    (init (cell 2 1 b))
    (init (cell 2 2 b))
    (init (cell 2 3 b))
    (init (cell 3 1 b))
    (init (cell 3 2 b))
    (init (cell 3 3 b))
    (init (control white))
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; legal
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        
    (<= (legal ?w (mark ?x ?y))
        (true (cell ?x ?y b))
        (true (control ?w)))
    
    (<= (legal white noop)
        (true (control black)))
    
    (<= (legal black noop)
        (true (control white)))
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; next
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

    (<= (next (cell ?m ?n x))
        (does white (mark ?m ?n))
        (true (cell ?m ?n b)))
    
    (<= (next (cell ?m ?n o))
        (does black (mark ?m ?n))
        (true (cell ?m ?n b)))
    
    (<= (next (cell ?m ?n ?w))
        (true (cell ?m ?n ?w))
        (distinct ?w b))
    
    (<= (next (cell ?m ?n b))
        (does ?w (mark ?j ?k))
        (true (cell ?m ?n b))
        (distinct ?m ?j))
    
    (<= (next (cell ?m ?n b))
        (does ?w (mark ?j ?k))
        (true (cell ?m ?n b))
        (distinct ?n ?k))
    
    (<= (next (control white))
        (true (control black)))
    
    (<= (next (control black))
        (true (control white)))
    
    
    (<= (row ?m ?x)
        (true (cell ?m 1 ?x))
        (true (cell ?m 2 ?x))
        (true (cell ?m 3 ?x)))
    
    (<= (column ?n ?x)
        (true (cell 1 ?n ?x))
        (true (cell 2 ?n ?x))
        (true (cell 3 ?n ?x)))
    
    (<= (diagonal ?x)
        (true (cell 1 1 ?x))
        (true (cell 2 2 ?x))
        (true (cell 3 3 ?x)))
    
    (<= (diagonal ?x)
        (true (cell 1 3 ?x))
        (true (cell 2 2 ?x))
        (true (cell 3 1 ?x)))
    
    
    (<= (line ?x) (row ?m ?x))
    (<= (line ?x) (column ?m ?x))
    (<= (line ?x) (diagonal ?x))
    
    
    (<= open (true (cell ?m ?n b)))
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; goal
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    
    (<= (goal white 100)
        (line x)
        (not (line o)))
    
    (<= (goal white 50)
        (not (line x))
        (not (line o)))
    
    (<= (goal white 0)
        (not (line x))
        (line o))

    (<= (goal black 100)
        (not (line x))
        (line o))
      
    (<= (goal black 50)
        (not (line x))
        (not (line o)))
  
    (<= (goal black 0)
        (line x)
        (not (line o)))
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; terminal
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    
    (<= terminal
        (line x))
    
    (<= terminal
        (line o))
    
    (<= terminal
        (not open))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
)";
  Initialize(tictactoe_kif, "tictactoe", backend);
}

const std::string& GetGameName() {
  return game_name;
}

StateSp CreateInitialState() {
  if (is_gdlcc_engine_initialized) {
    return gdlcc::CreateInitialState();
  } else {
    return yap::CreateInitialState();
  }
}

std::string GetGGPEPath() {
#ifdef GGPE_PATH
  return GGPE_PATH;
#else
  static_assert(false, "GGPE_PATH macro must be defined at compile time.");
#endif
}

EngineBackend GetEngineBackend() {
  if (is_gdlcc_engine_initialized) {
    return EngineBackend::GDLCC;
  } else {
    return EngineBackend::YAP;
  }
}

std::vector<int> GetPartialGoals(const StateSp& state) {
  return yap::GetPartialGoals(state);
}

std::vector<NextCondition> DetectNextConditions(const Fact& fact) {
  return yap::DetectNextConditions(fact);
}

const std::vector<std::vector<FactSet>>& GetWinConditions() {
  return win_conditions;
}

}
