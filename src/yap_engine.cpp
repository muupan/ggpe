#include "yap_engine.hpp"

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

namespace ggpe {

// Types
template <class K, class V>
using UnorderedBimap = boost::bimaps::bimap<boost::bimaps::unordered_set_of<K>, boost::bimaps::unordered_set_of<V>>;
using AtomAndYapAtom = UnorderedBimap<Atom, YAP_Atom>::value_type;
using AtomAndString = UnorderedBimap<Atom, std::string>::value_type;

// Constants
const auto kAtomOffset = 512;

// Global variables
extern UnorderedBimap<Atom, std::string> atom_to_string;
extern std::string game_name;
extern std::vector<Atom> roles;
extern std::vector<int> role_indices;
extern std::unordered_map<Atom, int> atom_to_role_index;
extern std::unordered_map<Atom, int> atom_to_goal_values;
extern std::vector<Tuple> initial_facts;
extern std::vector<Tuple> possible_facts;
extern std::vector<std::vector<Tuple>> possible_actions;
extern std::unordered_map<Atom, std::unordered_map<Atom, int>> atom_to_ordered_domain;
extern std::unordered_set<Atom> step_counter_atoms;
extern std::unordered_map<AtomPair, std::vector<std::pair<Atom, std::pair<int, int>>>, boost::hash<AtomPair>> fact_action_connections;
extern std::unordered_map<Atom, std::unordered_map<int, Atom>> fact_ordered_args;
extern std::unordered_map<Atom, std::unordered_map<int, Atom>> action_ordered_args;
extern std::string game_kif;
extern bool game_enables_tabling;
extern std::vector<std::vector<FactSet>> win_conditions;

namespace yap {

namespace {

// Types
using Mutex = std::mutex;

// Constants
const auto kPrefix = std::string("gdl_");

// Global variables
Mutex mutex;
UnorderedBimap<Atom, YAP_Atom> atom_to_yap_atom;
// []
YAP_Term empty_list_term;
// role/1
YAP_Functor state_role_functor;
// state_init/1
YAP_Functor state_init_functor;
// legal_all/3
YAP_Functor state_legal_functor;
// next_all/3
YAP_Functor state_next_functor;
YAP_Functor state_next_and_goal_functor;
// state_terminal/1
YAP_Functor state_terminal_functor;
// state_goal/2
YAP_Functor state_goal_functor;
// state_simulate/2
YAP_Functor state_simulate_functor;
// state_base/1
YAP_Functor state_base_functor;
// state_input/2
YAP_Functor state_input_functor;
// state_ordered_domain/1
YAP_Functor state_ordered_domain_functor;
// state_step_counter/1
YAP_Functor state_step_counter_functor;
// state_fact_action_connections/1
YAP_Functor state_fact_action_connections_functor;
// state_fact_ordered_args/1
YAP_Functor state_fact_ordered_args_functor;
// state_action_ordered_args/1
YAP_Functor state_action_ordered_args_functor;
// state_partial_goal/2
YAP_Functor state_partial_goal_functor;
// state_win_conditions/1
YAP_Functor state_win_conditions_functor;
// next_conditions/2
YAP_Functor next_conditions_functor;

// Utility functions
template <class SuccessHandler, class FailureHandler>
void RunWithSlot(
    const YAP_Term& goal,
    SuccessHandler success_handler,
    FailureHandler failure_handler) {
  auto slot = YAP_InitSlot(goal);
  const auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    success_handler(YAP_GetFromSlot(slot));
  } else {
    failure_handler();
  }
  YAP_Reset();
#ifdef _YAPDEFS_H // YAP 6.3
  YAP_RecoverSlots(1, slot);
#else // YAP 6.2
  YAP_RecoverSlots(1);
#endif
}

template <class SuccessHandler>
void RunWithSlotOrError(
    const YAP_Term& goal,
    SuccessHandler success_handler,
    const std::string& error_message="") {
  RunWithSlot(goal, success_handler, [&]{
    throw std::runtime_error(error_message);
  });
}

std::string AtomsToString(const std::vector<Atom>& atoms) {
  std::ostringstream o;
  o << '[';
  for (auto i = atoms.begin(); i != atoms.end(); ++i) {
    if (i != atoms.begin()) {
      o << ", ";
    }
    o << AtomToString(*i);
  }
  o << ']';
  return o.str();
}

YAP_Atom AtomToYapAtom(const Atom atom) {
#ifndef NDEBUG
  if (!atom_to_yap_atom.left.count(atom)) {
    std::cerr << "Cannot convert atom=" << atom << " to yap atom." << std::endl;
  }
#endif
  return atom_to_yap_atom.left.at(atom);
}

Atom YapAtomToAtom(const YAP_Atom yap_atom) {
#ifndef NDEBUG
  if (!atom_to_yap_atom.right.count(yap_atom)) {
    std::cerr << "Cannot convert yap_atom=" << YAP_AtomName(yap_atom) << " to atom." << std::endl;
  }
#endif
  return atom_to_yap_atom.right.at(yap_atom);
}

YAP_Atom StringToYapAtom(const std::string& atom_str) {
  assert(atom_to_yap_atom.left.count(StringToAtom(atom_str)));
  return AtomToYapAtom(StringToAtom(atom_str));
}

const std::string& YapAtomToString(const YAP_Atom yap_atom) {
  return AtomToString(YapAtomToAtom(yap_atom));
}

Atom YapTermToAtom(const YAP_Term term) {
  assert(YAP_IsAtomTerm(term));
  return YapAtomToAtom(YAP_AtomOfTerm(term));
}

YAP_Term AtomToYapTerm(const Atom atom) {
  return YAP_MkAtomTerm(AtomToYapAtom(atom));
}

std::string YapAtomTermToString(const YAP_Term& term) {
  assert(YAP_IsAtomTerm(term));
  return AtomToString(YapTermToAtom(term));
}

std::string YapTermToString(const YAP_Term& term);

std::string YapCompoundTermToString(const YAP_Term& term) {
  assert(YAP_IsApplTerm(term));
  const auto f = YAP_FunctorOfTerm(term);
  const auto n = static_cast<int>(YAP_ArityOfFunctor(f));
  std::ostringstream o;
  o << '(' << YapAtomToString(YAP_NameOfFunctor(f)) << ' ';
  for (auto i = 1; i <= n; ++i) {
    const auto arg = YAP_ArgOfTerm(i, term);
    o << YapTermToString(arg);
    if (i < n) {
      o << ' ';
    }
  }
  o << ')';
  return o.str();
}

std::string YapTermToString(const YAP_Term& term) {
  assert((YAP_IsAtomTerm(term) || YAP_IsApplTerm(term)) && "");
  if (YAP_IsAtomTerm(term)) {
    return YapAtomTermToString(term);
  } else {
    return YapCompoundTermToString(term);
  }
}

Tuple YapCompoundTermToTuple(const YAP_Term& term) {
  assert(YAP_IsApplTerm(term));
  // Compound term
  const auto functor = YAP_FunctorOfTerm(term);
  const auto arity = YAP_ArityOfFunctor(functor);
  Tuple tuple;
  // (arity + 1) is not enough in case of compound terms inside compound terms.
  // Thus, what should be done here is only reserving, not resizing.
  tuple.reserve(arity + 1);
  const auto functor_atom = YapAtomToAtom(YAP_NameOfFunctor(functor));
  tuple.push_back(functor_atom);
  for (auto i = 1; i <= static_cast<int>(arity); ++i) {
    const auto arg = YAP_ArgOfTerm(i, term);
    assert(YAP_IsAtomTerm(arg) || YAP_IsApplTerm(arg));
    if (YAP_IsAtomTerm(arg)) {
      const auto arg_atom = YapTermToAtom(arg);
      tuple.push_back(arg_atom);
    } else {
      // Compound terms inside compound terms are flattened between parens
      tuple.push_back(atoms::kLeftParen);
      const auto arg_tuple = YapCompoundTermToTuple(arg);
      tuple.insert(tuple.end(), arg_tuple.begin(), arg_tuple.end());
      tuple.push_back(atoms::kRightParen);
    }
  }
  return tuple;
}

Tuple YapTermToTuple(const YAP_Term& term) {
  assert(YAP_IsAtomTerm(term) || YAP_IsApplTerm(term));
  if (YAP_IsAtomTerm(term)) {
    return Tuple({YapTermToAtom(term)});
  } else {
    return YapCompoundTermToTuple(term);
  }
}

template <class Iterator>
YAP_Term CollectYapTerm(Iterator& it) {
  if (*it == atoms::kLeftParen) {
    ++it;
    assert(*it != atoms::kLeftParen);
    assert(*it != atoms::kRightParen);
    const auto functor_atom = AtomToYapAtom(*it);
    std::vector<YAP_Term> args;
    while (*it != atoms::kRightParen) {
      args.push_back(CollectYapTerm(it));
    }
    assert(*it == atoms::kRightParen);
    ++it;
    const auto functor = YAP_MkFunctor(functor_atom, args.size());
    return YAP_MkApplTerm(functor, args.size(), args.data());
  } else {
    assert(*it != atoms::kRightParen);
    return AtomToYapTerm(*(it++));
  }
}

YAP_Term TupleToYapTerm(const Tuple& tuple) {
//  std::cout << "TupleToYapTerm" << std::endl;
//  std::cout << "size=" << tuple.size() << std::endl;
//  for (const auto& atom : tuple) {
//    std::cout << atom << ' ' << AtomToString(atom) << std::endl;
//  }
//  std::cout << TupleToString(tuple) << std::endl;
  assert(!tuple.empty());
  if (tuple.size() == 1) {
//    std::cout << "size=1 atom=" << tuple.front() << std::endl;
//    std::cout << "yap_atom=" << YAP_AtomName(AtomToYapAtom(tuple.front())) << std::endl;
    return AtomToYapTerm(tuple.front());
  } else {
//    auto i = tuple.begin();
//    return TupleToYapTerm(i);
    const auto functor_atom = AtomToYapAtom(tuple.front());
    std::vector<YAP_Term> args;
    auto it = tuple.begin() + 1;
    while (it != tuple.end()) {
      args.push_back(CollectYapTerm(it));
    }
    const auto functor = YAP_MkFunctor(functor_atom, args.size());
    return YAP_MkApplTerm(functor, args.size(), args.data());
//    return TupleToYapTerm(i);
  }
}

std::vector<Tuple> YapPairTermToTuples(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term) || pair_term == empty_list_term);
  std::vector<Tuple> tuples;
  auto temp_term = pair_term;
  while (YAP_IsPairTerm(temp_term)) {
    tuples.push_back(YapTermToTuple(YAP_HeadOfTerm(temp_term)));
    temp_term = YAP_TailOfTerm(temp_term);
  }
  assert(temp_term == empty_list_term);
  return tuples;
}

std::vector<std::vector<Tuple>> YapPairTermToActions(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term));
  std::vector<std::vector<Tuple>> actions(GetRoleCount());
  auto temp_term = pair_term;
  auto pair_count = 0;
  while (YAP_IsPairTerm(temp_term)) {
    const auto pair = YAP_HeadOfTerm(temp_term);
    assert(YAP_IsPairTerm(pair));
    const auto role_term = YAP_HeadOfTerm(pair);
    assert(YAP_IsAtomTerm(role_term));
    const auto role_atom = YapTermToAtom(role_term);
    const auto action_term = YAP_HeadOfTerm(YAP_TailOfTerm(pair));
    assert(YAP_IsPairTerm(action_term));
    assert(atom_to_role_index.count(role_atom));
    const auto role_index = atom_to_role_index[role_atom];
    actions[role_index] = YapPairTermToTuples(action_term);
    temp_term = YAP_TailOfTerm(temp_term);
    ++pair_count;
  }
  assert(temp_term == empty_list_term);
  assert(pair_count == static_cast<int>(GetRoleCount()));
  return actions;
}

std::vector<int> YapPairTermToGoals(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term) || pair_term == empty_list_term);
  std::vector<int> goals(GetRoleCount());
  auto temp_term = pair_term;
  auto goal_count = 0;
  while (YAP_IsPairTerm(temp_term)) {
    const auto pair = YAP_HeadOfTerm(temp_term);
    assert(YAP_IsPairTerm(pair));
    const auto role_term = YAP_HeadOfTerm(pair);
    assert(YAP_IsAtomTerm(role_term));
    const auto role_atom = YapTermToAtom(role_term);
    const auto goal_term = YAP_HeadOfTerm(YAP_TailOfTerm(pair));
    assert(atom_to_role_index.count(role_atom));
    if (YAP_IsAtomTerm(goal_term)) {
      const auto goal_atom = YapTermToAtom(goal_term);
      assert(atom_to_goal_values.count(goal_atom));
      goals[atom_to_role_index[role_atom]] = atom_to_goal_values[goal_atom];
    } else {
      assert(YAP_IsIntTerm(goal_term));
      const auto goal_value = YAP_IntOfTerm(goal_term);
      assert(goal_value >= 0);
      assert(goal_value <= 100);
      goals[atom_to_role_index[role_atom]] = goal_value;
    }
    temp_term = YAP_TailOfTerm(temp_term);
    ++goal_count;
  }
  assert(temp_term == empty_list_term);
  assert(goal_count <= static_cast<int>(GetRoleCount()));
  if (goal_count < static_cast<int>(GetRoleCount())) {
    return std::vector<int>();
  }
  return goals;
}

std::vector<YAP_Term> YapPairTermToYapTerms(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term) || pair_term == empty_list_term);
  std::vector<YAP_Term> terms;
  auto temp_term = pair_term;
  while (YAP_IsPairTerm(temp_term)) {
    terms.push_back(YAP_HeadOfTerm(temp_term));
    temp_term = YAP_TailOfTerm(temp_term);
  }
  assert(temp_term == empty_list_term);
  return terms;
}

std::vector<Atom> YapPairTermToAtoms(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term) || pair_term == empty_list_term);
  std::vector<Atom> atoms;
  auto temp_term = pair_term;
  while (YAP_IsPairTerm(temp_term)) {
    const auto head = YAP_HeadOfTerm(temp_term);
    assert(YAP_IsAtomTerm(head));
    atoms.push_back(YapTermToAtom(head));
    temp_term = YAP_TailOfTerm(temp_term);
  }
  assert(temp_term == empty_list_term);
  return atoms;
}


YAP_Term TuplesToYapPairTerm(const std::vector<Tuple>& tuples) {
  auto temp = empty_list_term;
  for (const auto& tuple : tuples) {
    temp = YAP_MkPairTerm(TupleToYapTerm(tuple), temp);
  }
  return temp;
}

YAP_Term YapTermsToYapPairTerm(const YAP_Term& x, const YAP_Term& y) {
  return YAP_MkPairTerm(x, YAP_MkPairTerm(y, empty_list_term));
}

YAP_Term JointActionToYapPairTerm(const std::vector<Tuple>& joint_action) {
  assert(!joint_action.empty());
  auto temp = empty_list_term;
  for (const auto role_idx : GetRoleIndices()) {
    const auto role_action_pair =
        YapTermsToYapPairTerm(
            AtomToYapTerm(roles[role_idx]),
            TupleToYapTerm(joint_action[role_idx]));
    temp = YAP_MkPairTerm(role_action_pair, temp);
  }
  return temp;
}

void CacheConstantYapObjects() {
  empty_list_term = YAP_MkAtomTerm(YAP_FullLookupAtom("[]"));
  state_role_functor = YAP_MkFunctor(YAP_LookupAtom("state_role"), 1);
  state_init_functor = YAP_MkFunctor(YAP_LookupAtom("state_init"), 1);
  state_legal_functor = YAP_MkFunctor(YAP_LookupAtom("state_legal"), 2);
  state_next_functor = YAP_MkFunctor(YAP_LookupAtom("state_next"), 3);
  state_next_and_goal_functor = YAP_MkFunctor(YAP_LookupAtom("state_next_and_goal"), 4);
  state_terminal_functor = YAP_MkFunctor(YAP_LookupAtom("state_terminal"), 1);
  state_goal_functor = YAP_MkFunctor(YAP_LookupAtom("state_goal"), 2);
  state_simulate_functor = YAP_MkFunctor(YAP_LookupAtom("state_simulate"), 2);
  state_base_functor = YAP_MkFunctor(YAP_LookupAtom("state_base"), 1);
  state_input_functor = YAP_MkFunctor(YAP_LookupAtom("state_input"), 1);
  state_ordered_domain_functor = YAP_MkFunctor(YAP_LookupAtom("state_ordered_domain"), 1);
  state_step_counter_functor = YAP_MkFunctor(YAP_LookupAtom("state_step_counter"), 1);
  state_fact_action_connections_functor = YAP_MkFunctor(YAP_LookupAtom("state_fact_action_connections"), 1);
  state_fact_ordered_args_functor = YAP_MkFunctor(YAP_LookupAtom("state_fact_ordered_args"), 1);
  state_action_ordered_args_functor = YAP_MkFunctor(YAP_LookupAtom("state_action_ordered_args"), 1);
  state_partial_goal_functor = YAP_MkFunctor(YAP_LookupAtom("state_partial_goal"), 2);
  state_win_conditions_functor = YAP_MkFunctor(YAP_LookupAtom("state_win_conditions"), 1);
  next_conditions_functor = YAP_MkFunctor(YAP_LookupAtom("next_conditions"), 2);
}

void CacheRoles() {
  roles.clear();
  role_indices.clear();
  atom_to_role_index.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_role_functor, 1, args.data());
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto role_list_term = YAP_ArgOfTerm(1, result);
    const auto role_terms = YapPairTermToYapTerms(role_list_term);
    assert(!role_terms.empty() && "There must be at least one role.");
    for (const auto role_term : role_terms) {
      const auto role_atom = YapTermToAtom(role_term);
      roles.push_back(role_atom);
      role_indices.push_back(role_indices.size());
      atom_to_role_index.emplace(role_atom, atom_to_role_index.size());
    }
  }, "There must be at least one role.");
}

void CacheGoalValues(const std::unordered_set<std::string>& non_functor_atom_strs) {
  atom_to_goal_values.clear();
  for (const auto& atom_str : non_functor_atom_strs) {
    if (std::all_of(atom_str.begin(), atom_str.end(), ::isdigit)) {
      const auto value = std::stoi(atom_str);
      if (value >= 0 && value <= 100) {
        atom_to_goal_values.emplace(StringToAtom(atom_str), value);
      }
    }
  }
  assert(!atom_to_goal_values.empty() && "No goal is defined.");
}

void CacheInitialFacts() {
  initial_facts.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_init_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto pair_term = YAP_ArgOfTerm(1, result);
    const auto terms = YapPairTermToYapTerms(pair_term);
    for (const auto& term : terms) {
      const auto tuple = YapTermToTuple(term);
      initial_facts.push_back(tuple);
    }
  }, []{
    // It is possible that there is no initial fact.
    std::cout << "Note: no initial fact was found." << std::endl;
  });
}

void CachePossibleFacts() {
  possible_facts.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_base_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto pair_term = YAP_ArgOfTerm(1, result);
    const auto terms = YapPairTermToYapTerms(pair_term);
    for (const auto& term : terms) {
      const auto tuple = YapTermToTuple(term);
      possible_facts.push_back(tuple);
    }
  }, []{
    // 'base' relation was not found.
    std::cout << "Note: 'base' relation was not found." << std::endl;
  });
}

void CachePossibleActions() {
  possible_actions.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_input_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto role_actions_pairs_term = YAP_ArgOfTerm(1, result);
    possible_actions = YapPairTermToActions(role_actions_pairs_term);
  }, []{
    // 'input' relation was not found.
    std::cout << "Note: 'input' relation was not found." << std::endl;
  });
}

void DetectStepCounters() {
  step_counter_atoms.clear();
  std::cout << "Detecting step counters..." << std::endl;
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_step_counter_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto step_counter_atoms_term = YAP_ArgOfTerm(1, result);
    assert(YAP_IsPairTerm(step_counter_atoms_term));
    const auto atoms = YapPairTermToAtoms(step_counter_atoms_term);
    std::cout << "Step counters: " << AtomsToString(atoms) << std::endl;
    step_counter_atoms.insert(atoms.begin(), atoms.end());
  }, []{
    std::cout << "Note: no step counter was found." << std::endl;
  });
}

void DetectOrderedDomains() {
  std::cout << "Detecting ordered domains..." << std::endl;
  atom_to_ordered_domain.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_ordered_domain_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto relation_domain_pairs_term = YAP_ArgOfTerm(1, result);
    const auto relation_domain_pair_terms = YapPairTermToYapTerms(relation_domain_pairs_term);
    for (const auto& relation_domain_pair_term : relation_domain_pair_terms) {
      const auto terms = YapPairTermToYapTerms(relation_domain_pair_term);
      assert(terms.size() == 2);
      // Relation atom
      const auto relation_atom_term = terms.front();
      assert(YAP_IsAtomTerm(relation_atom_term));
      const auto relation_atom = YapTermToAtom(relation_atom_term);
      // Domain atoms
      const auto domain_term = terms.back();
      assert(YAP_IsPairTerm(domain_term));
      const auto domain_atoms = YapPairTermToAtoms(domain_term);
      std::cout << "Domain by " << AtomToString(relation_atom) << ": " << AtomsToString(domain_atoms) << std::endl;
      std::unordered_map<Atom, int> domain_map;
      for (const auto atom : domain_atoms) {
        domain_map.emplace(atom, domain_map.size());
      }
      atom_to_ordered_domain.emplace(relation_atom, domain_map);
    }
  }, []{
    std::cout << "Note: no ordered domain was found." << std::endl;
  });
}

std::pair<int, int> YapPairTermToIntPair(const YAP_Term& term) {
  const auto& terms = YapPairTermToYapTerms(term);
  assert(terms.size() == 2);
  assert(YAP_IsIntTerm(terms.front()));
  assert(YAP_IsIntTerm(terms.back()));
  const auto first_arg = YAP_IntOfTerm(terms.front());
  const auto second_arg = YAP_IntOfTerm(terms.back());
  return std::make_pair(first_arg, second_arg);
}

void DetectFactActionConnections() {
  std::cout << "Detecting fact-action connections..." << std::endl;
  fact_action_connections.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_fact_action_connections_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto connections_term = YAP_ArgOfTerm(1, result);
    assert(YAP_IsPairTerm(connections_term));
    const auto connection_terms = YapPairTermToYapTerms(connections_term);
    for (const auto& connection_term : connection_terms) {
      assert(YAP_IsPairTerm(connection_term));
      const auto connection_term_terms = YapPairTermToYapTerms(connection_term);
      assert(connection_term_terms.size() == 3);
      // Fact atom
      const auto& fact_atom_term = connection_term_terms[0];
      assert(YAP_IsAtomTerm(fact_atom_term));
      const auto fact_atom = YapTermToAtom(fact_atom_term);
      // Action atom
      const auto& action_atom_term = connection_term_terms[1];
      assert(YAP_IsAtomTerm(action_atom_term));
      const auto action_atom = YapTermToAtom(action_atom_term);
      // Args atom
      const auto& args_term = connection_term_terms[2];
      assert(YAP_IsPairTerm(args_term));
      const auto& args_term_terms = YapPairTermToYapTerms(args_term);
      std::vector<std::pair<Atom, std::pair<int, int>>> args;
      for (const auto& args_term_term : args_term_terms) {
        const auto terms = YapPairTermToYapTerms(args_term_term);
        assert(terms.size() == 2);
        // Order relation atom
        const auto& order_relation_atom_term = terms[0];
        assert(YAP_IsAtomTerm(order_relation_atom_term));
        const auto order_relation_atom = YapTermToAtom(order_relation_atom_term);
      // Args atom
        // Arg pair term
        const auto& arg_pair_term = terms[1];
        const auto& pair = YapPairTermToIntPair(arg_pair_term);
        args.emplace_back(order_relation_atom, pair);
      }
      fact_action_connections.emplace(std::make_pair(fact_atom, action_atom), args);
    }
  }, []{
    std::cout << "Note: no fact-action connection was found." << std::endl;
  });
  for (const auto& entry : fact_action_connections) {
    const auto fact_atom = entry.first.first;
    const auto action_atom = entry.first.second;
    std::cout << "Fact-Action connection: (" << AtomToString(fact_atom) << ' ' << AtomToString(action_atom) << ')';
    for (const auto& rel_args_pair : entry.second) {
      const auto order_rel = rel_args_pair.first;
      const auto& arg_pair = rel_args_pair.second;
      std::cout << ' ' << AtomToString(order_rel) << '(' << arg_pair.first << ' ' << arg_pair.second << ')';
    }
    std::cout << std::endl;
  }
}

void DetectFactOrderedArgs() {
  fact_ordered_args.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_fact_ordered_args_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto fact_ordered_args_pairs_term = YAP_ArgOfTerm(1, result);
    const auto fact_ordered_args_pair_terms = YapPairTermToYapTerms(fact_ordered_args_pairs_term);
    for (const auto& fact_ordered_args_pair_term : fact_ordered_args_pair_terms) {
      const auto fact_term_and_ordered_args_term = YapPairTermToYapTerms(fact_ordered_args_pair_term);
      assert(fact_term_and_ordered_args_term.size() == 2);
      const auto& fact_rel_term = fact_term_and_ordered_args_term.at(0);
      assert(YAP_IsAtomTerm(fact_rel_term));
      const auto fact_rel_atom = YapTermToAtom(fact_rel_term);
      const auto& ordered_args_term = fact_term_and_ordered_args_term.at(1);
      const auto ordered_arg_terms = YapPairTermToYapTerms(ordered_args_term);
      std::unordered_map<int, Atom> arg_order_rel_map;
      for (const auto& ordered_arg_term : ordered_arg_terms) {
        const auto arg_term_and_order_rel_term = YapPairTermToYapTerms(ordered_arg_term);
        assert(arg_term_and_order_rel_term.size() == 2);
        const auto& arg_term = arg_term_and_order_rel_term.at(0);
        assert(YAP_IsIntTerm(arg_term));
        const auto arg = YAP_IntOfTerm(arg_term);
        const auto& order_rel_term = arg_term_and_order_rel_term.at(1);
        assert(YAP_IsAtomTerm(order_rel_term));
        const auto order_rel_atom = YapTermToAtom(order_rel_term);
        arg_order_rel_map.emplace(arg, order_rel_atom);
      }
      fact_ordered_args.emplace(fact_rel_atom, arg_order_rel_map);
    }
  }, []{
    std::cout << "Note: no ordered arguments of facts were found." << std::endl;
  });
  for (const auto& fact_ordered_args_pair : fact_ordered_args) {
    std::cout << "Ordered args of fact " << AtomToString(fact_ordered_args_pair.first) << ':';
    for (const auto& arg_order_rel_pair : fact_ordered_args_pair.second) {
      std::cout << " [" << arg_order_rel_pair.first << ", " << AtomToString(arg_order_rel_pair.second) << ']';
    }
    std::cout << std::endl;
  }
}

void DetectActionOrderedArgs() {
  action_ordered_args.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_action_ordered_args_functor, 1, args.data());
  RunWithSlot(goal, [&](const YAP_Term& result){
    const auto action_ordered_args_pairs_term = YAP_ArgOfTerm(1, result);
    const auto action_ordered_args_pair_terms = YapPairTermToYapTerms(action_ordered_args_pairs_term);
    for (const auto& action_ordered_args_pair_term : action_ordered_args_pair_terms) {
      const auto action_term_and_ordered_args_term = YapPairTermToYapTerms(action_ordered_args_pair_term);
      assert(action_term_and_ordered_args_term.size() == 2);
      const auto& action_rel_term = action_term_and_ordered_args_term.at(0);
      assert(YAP_IsAtomTerm(action_rel_term));
      const auto action_rel_atom = YapTermToAtom(action_rel_term);
      const auto& ordered_args_term = action_term_and_ordered_args_term.at(1);
      const auto ordered_arg_terms = YapPairTermToYapTerms(ordered_args_term);
      std::unordered_map<int, Atom> arg_order_rel_map;
      for (const auto& ordered_arg_term : ordered_arg_terms) {
        const auto arg_term_and_order_rel_term = YapPairTermToYapTerms(ordered_arg_term);
        assert(arg_term_and_order_rel_term.size() == 2);
        const auto& arg_term = arg_term_and_order_rel_term.at(0);
        assert(YAP_IsIntTerm(arg_term));
        const auto arg = YAP_IntOfTerm(arg_term);
        const auto& order_rel_term = arg_term_and_order_rel_term.at(1);
        assert(YAP_IsAtomTerm(order_rel_term));
        const auto order_rel_atom = YapTermToAtom(order_rel_term);
        arg_order_rel_map.emplace(arg, order_rel_atom);
      }
      action_ordered_args.emplace(action_rel_atom, arg_order_rel_map);
    }
  }, []{
    std::cout << "Note: no ordered arguments of actions were found." << std::endl;
  });
  for (const auto& action_ordered_args_pair : action_ordered_args) {
    std::cout << "Ordered args of action " << AtomToString(action_ordered_args_pair.first) << ':';
    for (const auto& arg_order_rel_pair : action_ordered_args_pair.second) {
      std::cout << " [" << arg_order_rel_pair.first << ", " << AtomToString(arg_order_rel_pair.second) << ']';
    }
    std::cout << std::endl;
  }
}

/**
 * Construct atom dictionary:
 *   atom_to_string, atom_to_yap_atom
 * @param functor_atom_strs
 * @param non_functor_atom_strs
 */
void ConstructAtomDictionary(
    const std::unordered_set<std::string>& atom_strs) {
  atom_to_string.clear();
  atom_to_yap_atom.clear();
  // GDL atoms
  std::set<std::string> sorted_atom_strs(atom_strs.begin(), atom_strs.end());
  for (const auto& atom_str : sorted_atom_strs) {
    // Assign atom id for each atom string
    const auto atom = atom_to_string.size() + kAtomOffset;
    std::cout << atom_str << " -> " << atom << std::endl;
    atom_to_string.insert(AtomAndString(atom, atom_str));
    // Paring atom id and YAP_Atom
    const auto atom_str_with_prefix = kPrefix + atom_str;
    const auto yap_atom = YAP_LookupAtom(atom_str_with_prefix.c_str());
    atom_to_yap_atom.insert(AtomAndYapAtom(atom, yap_atom));
  }
  // Other atoms
  atom_to_string.insert(AtomAndString(atoms::kFree, "?"));
  // Atoms relative to free atom: ?-255, ?-254, ..., ?+255
  for (auto atom = atoms::kFree - 255; atom <= atoms::kFree + 255; ++atom) {
    if (atom == atoms::kFree) {
      continue;
    }
    const auto str = (boost::format("?%1$+d") % atom).str();
    atom_to_string.insert(AtomAndString(atom, str));
  }
  atom_to_string.insert(AtomAndString(atoms::kLeftParen, "("));
  atom_to_string.insert(AtomAndString(atoms::kRightParen, ")"));
}


void DetectWinConditions() {
  win_conditions.clear();
  win_conditions.resize(GetRoleCount());
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_win_conditions_functor, 1, args.data());
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto role_win_conditions_pair_terms =
        YapPairTermToYapTerms(YAP_ArgOfTerm(1, result));
    assert(role_win_conditions_pair_terms.size() == GetRoleCount());
    for (const auto& role_win_conditions_pair_term : role_win_conditions_pair_terms) {
      const auto role_win_conditions_pair = YapPairTermToYapTerms(role_win_conditions_pair_term);
      assert(role_win_conditions_pair.size() == 2);
      const auto& role_term = role_win_conditions_pair.front();
      const auto role_idx = atom_to_role_index.at(YapTermToAtom(role_term));
      const auto& conditions_term = role_win_conditions_pair.back();
      const auto condition_terms = YapPairTermToYapTerms(conditions_term);
      for (const auto& condition_term : condition_terms) {
        const auto condition = YapPairTermToTuples(condition_term);
        std::cout << "Win condition for " << role_idx << ":";
        for (const auto& fact : condition) {
          std::cout << TupleToString(fact) << ",";
        }
        std::cout << std::endl;
        win_conditions.at(role_idx).push_back(condition);
      }
    }
  });
}

//void LoadPrologFile(const std::string& filename) {
//  YAP_Term err;
//  auto goal = YAP_ReadBuffer(
//      (boost::format("compile('%1%')") %
//          tmp_pl_path.string()).str().c_str(), &err);
//  YAP_RunGoalOnce(goal);
//}

void RunGoalOnce(const std::string& query) {
  YAP_Term err;
  auto goal = YAP_ReadBuffer(query.c_str(), &err);
#ifdef _YAPDEFS_H // YAP 6.3
  auto slot = YAP_InitSlot(goal);
#else // YAP 6.2
  YAP_InitSlot(goal);
#endif
  YAP_RunGoalOnce(goal);
  YAP_Reset();
#ifdef _YAPDEFS_H // YAP 6.3
  YAP_RecoverSlots(1, slot);
#else // YAP 6.2
  YAP_RecoverSlots(1);
#endif
}

void CompilePrologFile(const std::string& prolog_filename) {
  RunGoalOnce((boost::format("compile('%1%')") % prolog_filename).str());
}

void InitializePrologEngineWithInterface() {
  const auto interface_binary_path =
      boost::filesystem::absolute(
          boost::filesystem::path("tmp/interface.yap"));
  if (!boost::filesystem::exists(interface_binary_path)) {
    const auto interface_prolog_path =
        boost::filesystem::absolute(
            boost::filesystem::path(GetGGPEPath() + "/interface.pl"));
    const auto compile_command =
        (boost::format("yap -z \"compile('%1%'), save_program('%2%'), halt\"") %
            interface_prolog_path.string() %
            interface_binary_path.string()).str();
    std::cout << compile_command << std::endl;
    std::system(compile_command.c_str());
  }
  assert(boost::filesystem::exists(interface_binary_path));
  YAP_FastInit(interface_binary_path.c_str());
  // Disable atom garbage collection
  YAP_SetYAPFlag(YAPC_ENABLE_AGC, 0);
}

void InitializePrologEngine(
    const std::vector<sexpr_parser::TreeNode>& kif_nodes,
    const bool enables_tabling) {
  assert(!kif_nodes.empty());
  assert(!game_name.empty());
  InitializePrologEngineWithInterface();
  const auto tmp_dir = boost::filesystem::path("tmp");
  const auto game_prolog_path =
      boost::filesystem::absolute(
          tmp_dir / boost::filesystem::path(game_name + ".pl"));
  std::ofstream ofs(game_prolog_path.string());
  ofs << sexpr_parser::ToProlog(
      kif_nodes,
      true,
      kPrefix,
      kPrefix,
      true,
      enables_tabling);
  ofs.close();
  CompilePrologFile(game_prolog_path.string());
}

}

void InitializeYapEngine(
    const std::string& kif,
    const std::string& name,
    const bool enables_tabling) {
  assert(!kif.empty());
  assert(!name.empty());
  const auto nodes = sexpr_parser::ParseKIF(kif);
  InitializePrologEngine(nodes, enables_tabling);
  // Now YAP Prolog is available
  const auto atom_strs = sexpr_parser::CollectAtoms(nodes);
  ConstructAtomDictionary(atom_strs);
  CacheGoalValues(atom_strs);
  CacheConstantYapObjects();
  CacheRoles();
  CacheInitialFacts();
  CachePossibleFacts();
  CachePossibleActions();
  DetectOrderedDomains();
  DetectStepCounters();
  DetectFactActionConnections();
  DetectWinConditions();
//  DetectFactOrderedArgs();
//  DetectActionOrderedArgs();
  if (enables_tabling) {
    RunGoalOnce("tabling_statistics");
  }
}

std::vector<int> GetPartialGoals(const StateSp& state) {
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  const auto fact_term = TuplesToYapPairTerm(state->GetFacts());
  std::array<YAP_Term, 2> args = {{ fact_term, YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_partial_goal_functor, 2, args.data());
  std::vector<int> goals;
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto role_goal_pairs_term = YAP_ArgOfTerm(2, result);
    goals = YapPairTermToGoals(role_goal_pairs_term);
    assert(!goals.empty());
  });
  return goals;
}

std::vector<NextCondition> DetectNextConditions(const Fact& fact) {
  std::vector<NextCondition> next_conditions;
  std::array<YAP_Term, 2> args = {{ TupleToYapTerm(fact), YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(next_conditions_functor, 2, args.data());
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto next_condition_terms =
        YapPairTermToYapTerms(YAP_ArgOfTerm(2, result));
    for (const auto& next_condition_term : next_condition_terms) {
      const auto action_condition_fact_condition = YapPairTermToYapTerms(next_condition_term);
      assert(action_condition_fact_condition.size() == 2);
      // Action condition
      const auto& action_condition_term = action_condition_fact_condition.front();
      const auto role_action_pair_terms = YapPairTermToYapTerms(action_condition_term);
      ActionCondition action_condition(GetRoleCount(), boost::none);
      for (const auto& role_action_pair_term : role_action_pair_terms) {
        const auto role_action_pair = YapPairTermToYapTerms(role_action_pair_term);
        assert(role_action_pair.size() == 2);
        const auto& role_term = role_action_pair.front();
        const auto role_idx = atom_to_role_index.at(YapTermToAtom(role_term));
        const auto& action_term = role_action_pair.back();
        const auto action = YapTermToTuple(action_term);
        action_condition[role_idx] = action;
      }
      // Fact condition
      const auto& fact_condition_term = action_condition_fact_condition.back();
      const auto fact_condition = YapPairTermToTuples(fact_condition_term);
      next_conditions.emplace_back(action_condition, fact_condition);
    }
  });
  return next_conditions;
}

StateSp CreateInitialState() {
  return std::make_shared<YapState>(initial_facts, std::vector<JointAction>());
}

YapState::YapState(const std::vector<Tuple>& facts, const std::vector<JointAction>& joint_action_history) :
    facts_(facts),
    legal_actions_(0),
    is_terminal_(false),
    goals_(0),
    joint_action_history_(joint_action_history) {
}

YapState::YapState(
    const std::vector<Tuple>& facts,
    const std::vector<int>& goals,
    const std::vector<JointAction>& joint_action_history) :
        facts_(facts),
        legal_actions_(0),
        is_terminal_(!goals.empty()),
        goals_(goals),
        joint_action_history_(joint_action_history) {
}

YapState::YapState(const YapState& another) :
    facts_(another.facts_),
    legal_actions_(another.legal_actions_),
    is_terminal_(another.is_terminal_),
    goals_(another.goals_),
    joint_action_history_(another.joint_action_history_) {
}

const FactSet& YapState::GetFacts() const {
  return facts_;
}

std::string YapState::ToString() const {
  std::ostringstream o;
  for (const auto& fact : facts_) {
    o << TupleToString(fact) << std::endl;
  }
  return o.str();
}

const std::vector<std::vector<Tuple>>& YapState::GetLegalActions() const {
  if (!legal_actions_.empty()) {
    return legal_actions_;
  }
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  const auto fact_term = TuplesToYapPairTerm(facts_);
  std::array<YAP_Term, 2> args = {{ fact_term, YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_legal_functor, 2, args.data());
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto role_actions_pairs_term = YAP_ArgOfTerm(2, result);
    legal_actions_ = YapPairTermToActions(role_actions_pairs_term);
  }, "Every role must always have at least one legal action.");
  return legal_actions_;
}

StateSp YapState::GetNextState(const JointAction& joint_action) const {
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  StateSp next_state;
  std::array<YAP_Term, 4> args = {{ TuplesToYapPairTerm(facts_), JointActionToYapPairTerm(joint_action), YAP_MkVarTerm(), YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_next_and_goal_functor, 4, args.data());
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto facts_term = YAP_ArgOfTerm(3, result);
    const auto goal_term = YAP_ArgOfTerm(4, result);
    auto next_joint_action_history = joint_action_history_;
    next_joint_action_history.push_back(joint_action);
    next_state = std::make_shared<YapState>(YapPairTermToTuples(facts_term), YapPairTermToGoals(goal_term), next_joint_action_history);
  });
  return next_state;
}

bool YapState::IsTerminal() const {
  return is_terminal_;
}

const std::vector<int>& YapState::GetGoals() const {
  assert(is_terminal_);
  if (!goals_.empty()) {
    return goals_;
  }
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  const auto fact_term = TuplesToYapPairTerm(facts_);
  std::array<YAP_Term, 2> args = {{ fact_term, YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_goal_functor, 2, args.data());
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto role_goal_pairs_term = YAP_ArgOfTerm(2, result);
    goals_ = YapPairTermToGoals(role_goal_pairs_term);
    assert(!goals_.empty());
  });
  return goals_;
}

std::vector<int> YapState::Simulate() const {
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  const auto fact_term = TuplesToYapPairTerm(facts_);
  std::array<YAP_Term, 2> args = {{ fact_term, YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_simulate_functor, 2, args.data());
  Goals goals;
  RunWithSlotOrError(goal, [&](const YAP_Term& result){
    const auto role_goal_pairs_term = YAP_ArgOfTerm(2, result);
    goals = YapPairTermToGoals(role_goal_pairs_term);
  });
  return goals;
}

const std::vector<JointAction>& YapState::GetJointActionHistory() const {
  return joint_action_history_;
}

}
}
