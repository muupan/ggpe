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

namespace ggpe {

const auto kFunctorPrefix = std::string("f_");
const auto kAtomPrefix = std::string("a_");

using Mutex = std::mutex;
Mutex mutex;

template <class K, class V>
using UnorderedBimap = boost::bimaps::bimap<boost::bimaps::unordered_set_of<K>, boost::bimaps::unordered_set_of<V>>;
using AtomAndString = UnorderedBimap<Atom, std::string>::value_type;
UnorderedBimap<Atom, std::string> atom_to_string;
using AtomAndYapAtom = UnorderedBimap<Atom, YAP_Atom>::value_type;
UnorderedBimap<Atom, YAP_Atom> atom_to_yap_atom;
std::string game_name;
std::vector<Atom> roles;
std::vector<int> role_indices;
std::unordered_map<Atom, int> atom_to_role_index;
std::unordered_map<Atom, int> atom_to_functor_arity;
std::unordered_map<Atom, int> atom_to_goal_values;
std::unordered_map<std::string, int> functor_name_to_arity;
std::vector<Tuple> initial_facts;
std::vector<Tuple> possible_facts;
std::vector<std::vector<Tuple>> possible_actions;
std::unordered_map<Atom, std::unordered_map<Atom, int>> atom_to_ordered_domain;
std::unordered_set<Atom> step_counter_atoms;
std::unordered_map<AtomPair, std::vector<std::pair<Atom, std::pair<int, int>>>, boost::hash<AtomPair>> fact_action_connections;
std::unordered_map<Atom, std::unordered_map<int, Atom>> fact_ordered_args;
std::unordered_map<Atom, std::unordered_map<int, Atom>> action_ordered_args;
bool next_state_caching_enabled = false;

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

const std::string& AtomToString(const Atom atom) {
  return atom_to_string.left.at(atom);
}

Atom StringToAtom(const std::string& atom_str) {
  return atom_to_string.right.at(atom_str);
}

YAP_Atom AtomToYapAtom(const Atom atom) {
  return atom_to_yap_atom.left.at(atom);
}

Atom YapAtomToAtom(const YAP_Atom yap_atom) {
  return atom_to_yap_atom.right.at(yap_atom);
}

YAP_Atom StringToYapAtom(const std::string& atom_str) {
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
      // Compound terms are flattened
      const auto arg_tuple = YapCompoundTermToTuple(arg);
      tuple.insert(tuple.end(), arg_tuple.begin(), arg_tuple.end());
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
YAP_Term TupleToYapTerm(Iterator& pos) {
//  static_assert(std::is_same<decltype(*pos), Atom>::value, "");
  if (!atom_to_functor_arity.count(*pos)) {
    return AtomToYapTerm(*(pos++));
  }
  const auto arity = atom_to_functor_arity[*pos];
  const auto functor = YAP_MkFunctor(AtomToYapAtom(*pos), arity);
  ++pos;
  std::vector<YAP_Term> args(arity);
  for (auto i = 0; i < arity; ++i) {
    args[i] = TupleToYapTerm(pos);
  }
  return YAP_MkApplTerm(functor, arity, args.data());
}

YAP_Term TupleToYapTerm(const Tuple& tuple) {
  assert(!tuple.empty());
  auto i = tuple.begin();
  return TupleToYapTerm(i);
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
  std::vector<std::vector<Tuple>> actions(roles.size());
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
    const auto role_index = atom_to_role_index.at(role_atom);
    actions[role_index] = YapPairTermToTuples(action_term);
    temp_term = YAP_TailOfTerm(temp_term);
    ++pair_count;
  }
  assert(temp_term == empty_list_term);
  assert(pair_count == static_cast<int>(roles.size()));
  return actions;
}

std::vector<int> YapPairTermToGoals(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term) || pair_term == empty_list_term);
  std::vector<int> goals(roles.size());
  auto temp_term = pair_term;
  auto goal_count = 0;
  while (YAP_IsPairTerm(temp_term)) {
    const auto pair = YAP_HeadOfTerm(temp_term);
    assert(YAP_IsPairTerm(pair));
    const auto role_term = YAP_HeadOfTerm(pair);
    assert(YAP_IsAtomTerm(role_term));
    const auto role_atom = YapTermToAtom(role_term);
    const auto goal_term = YAP_HeadOfTerm(YAP_TailOfTerm(pair));
    assert(YAP_IsAtomTerm(goal_term));
    const auto goal_atom = YapTermToAtom(goal_term);
    goals[atom_to_role_index[role_atom]] = atom_to_goal_values[goal_atom];
    temp_term = YAP_TailOfTerm(temp_term);
    ++goal_count;
  }
  assert(temp_term == empty_list_term);
  assert(goal_count <= static_cast<int>(roles.size()));
  if (goal_count < static_cast<int>(roles.size())) {
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
  for (const auto role_idx : role_indices) {
    const auto role_action_pair = YapTermsToYapPairTerm(AtomToYapTerm(roles[role_idx]), TupleToYapTerm(joint_action[role_idx]));
    temp = YAP_MkPairTerm(role_action_pair, temp);
  }
  return temp;
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

template <class Iterator>
std::string TupleToString(const Iterator& begin, const Iterator& end) {
  assert(std::distance(begin, end) > 0);
  if (std::distance(begin, end) == 1) {
    // Atom
    return std::string(AtomToString(*begin));
  }
  // Compound term
  std::ostringstream o;
  o << '(';
  o << AtomToString(*begin) << ' ';
  for (auto i = begin + 1; i != end; ++i) {
    const auto atom = *i;
//    o << AtomToString(atom);
    if (atom_to_functor_arity.count(atom)) {
      // Functor atom
      const auto arity = atom_to_functor_arity[atom];
      o << TupleToString(i, i + arity);
      i += arity - 1;
    } else {
      // Non-functor atom
      o << AtomToString(atom);
    }
    if (i != end - 1) {
      o << ' ';
    }
  }
  o << ')';
  return o.str();
}

std::string TupleToString(const Tuple& tuple) {
  return TupleToString(tuple.begin(), tuple.end());
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

void CacheRoles() {
  roles.clear();
  role_indices.clear();
  atom_to_role_index.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_role_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
#ifdef NDEBUG
  YAP_RunGoalOnce(goal);
#else
  auto okay = YAP_RunGoalOnce(goal);
  assert(okay && "There must be at least one role.");
#endif
  const auto result = YAP_GetFromSlot(slot);
  const auto role_list_term = YAP_ArgOfTerm(1, result);
  const auto role_terms = YapPairTermToYapTerms(role_list_term);
  assert(!role_terms.empty() && "There must be at least one role.");
  for (const auto role_term : role_terms) {
    const auto role_atom = YapTermToAtom(role_term);
    roles.push_back(role_atom);
    role_indices.push_back(role_indices.size());
    atom_to_role_index.emplace(role_atom, atom_to_role_index.size());
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
}

void CacheInitialFacts() {
  initial_facts.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_init_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  // It is possible that there is no initial fact.
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
    const auto pair_term = YAP_ArgOfTerm(1, result);
    const auto terms = YapPairTermToYapTerms(pair_term);
    for (const auto& term : terms) {
      const auto tuple = YapTermToTuple(term);
      initial_facts.push_back(tuple);
    }
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
}

void CachePossibleFacts() {
  possible_facts.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_base_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  // It is possible that there is no initial fact.
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
    const auto pair_term = YAP_ArgOfTerm(1, result);
    const auto terms = YapPairTermToYapTerms(pair_term);
    for (const auto& term : terms) {
      const auto tuple = YapTermToTuple(term);
      possible_facts.push_back(tuple);
    }
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
}

void CachePossibleActions() {
  possible_actions.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_input_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
    const auto role_actions_pairs_term = YAP_ArgOfTerm(1, result);
    possible_actions = YapPairTermToActions(role_actions_pairs_term);
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
}

void DetectStepCounters() {
  std::cout << "Detecting step counters..." << std::endl;
  step_counter_atoms.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_step_counter_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
    const auto step_counter_atoms_term = YAP_ArgOfTerm(1, result);
    assert(YAP_IsPairTerm(step_counter_atoms_term));
    const auto atoms = YapPairTermToAtoms(step_counter_atoms_term);
    std::cout << "Step counters: " << AtomsToString(atoms) << std::endl;
    step_counter_atoms.insert(atoms.begin(), atoms.end());
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
}

void DetectOrderedDomains() {
  std::cout << "Detecting ordered domains..." << std::endl;
  atom_to_ordered_domain.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_ordered_domain_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
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
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
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
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
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
  }
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
  YAP_Reset();
  YAP_RecoverSlots(1);
}

void DetectFactOrderedArgs() {
  fact_ordered_args.clear();
  std::array<YAP_Term, 1> args = {{ YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_fact_ordered_args_functor, 1, args.data());
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
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
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
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
  auto slot = YAP_InitSlot(goal);
  auto okay = YAP_RunGoalOnce(goal);
  if (okay) {
    const auto result = YAP_GetFromSlot(slot);
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
  }
  YAP_Reset();
  YAP_RecoverSlots(1);
  for (const auto& action_ordered_args_pair : action_ordered_args) {
    std::cout << "Ordered args of action " << AtomToString(action_ordered_args_pair.first) << ':';
    for (const auto& arg_order_rel_pair : action_ordered_args_pair.second) {
      std::cout << " [" << arg_order_rel_pair.first << ", " << AtomToString(arg_order_rel_pair.second) << ']';
    }
    std::cout << std::endl;
  }
}

void ConstructAtomDictionary(const std::unordered_map<std::string, int>& functor_atom_strs, const std::unordered_set<std::string>& non_functor_atom_strs) {
  std::vector<std::string> sorted_functor_atom_strs;
  sorted_functor_atom_strs.reserve(functor_atom_strs.size());
  for (const auto& pair : functor_atom_strs) {
    sorted_functor_atom_strs.push_back(pair.first);
  }
  std::sort(sorted_functor_atom_strs.begin(), sorted_functor_atom_strs.end());
  std::vector<std::string> sorted_non_functor_atom_strs(non_functor_atom_strs.begin(), non_functor_atom_strs.end());
  std::sort(sorted_non_functor_atom_strs.begin(), sorted_non_functor_atom_strs.end());
  atom_to_string.clear();
  atom_to_yap_atom.clear();
  constexpr Atom atom_offset = 256;
  for (const auto& atom_str : sorted_functor_atom_strs) {
    // Assign atom id for each atom string
    const auto atom = atom_to_string.size() + atom_offset;
    atom_to_string.insert(AtomAndString(atom, atom_str));
    // Paring atom id and YAP_Atom
    const auto atom_str_with_prefix = kFunctorPrefix + atom_str;
    const auto yap_atom = YAP_LookupAtom(atom_str_with_prefix.c_str());
    atom_to_yap_atom.insert(AtomAndYapAtom(atom, yap_atom));
  }
  for (const auto& atom_str : sorted_non_functor_atom_strs) {
    // Assign atom id for each atom string
    const auto atom = atom_to_string.size() + atom_offset;
    atom_to_string.insert(AtomAndString(atom, atom_str));
    // Paring atom id and YAP_Atom
    const auto atom_str_with_prefix = kAtomPrefix + atom_str;
    const auto yap_atom = YAP_LookupAtom(atom_str_with_prefix.c_str());
    atom_to_yap_atom.insert(AtomAndYapAtom(atom, yap_atom));
  }
  // Reserved atoms
  atom_to_string.insert(AtomAndString(atoms::kFree, "?"));
}

void Initialize(const std::string& kif, const std::string& name) {
  assert(!kif.empty());
  assert(!name.empty());
  game_name = name;
  const auto nodes = sexpr_parser::ParseKIF(kif);
  assert(!nodes.empty());
  const auto tmp_pl_filename = name + ".pl";
  const auto tmp_yap_filename = name + ".yap";
  if (!boost::filesystem::exists(boost::filesystem::path(tmp_yap_filename))) {
    if (!boost::filesystem::exists(boost::filesystem::path(tmp_pl_filename))) {
      std::ofstream ofs(tmp_pl_filename);
      ofs << sexpr_parser::ToProlog(nodes, true, kFunctorPrefix, kAtomPrefix, true);
      ofs.close();
    }
    const auto compile_command = (boost::format("yap -g \"compile(['%s', 'interface.pl']), save_program('%s'), halt.\"") % tmp_pl_filename % tmp_yap_filename).str();
    std::system(compile_command.c_str());
  }
  YAP_FastInit(tmp_yap_filename.c_str());
  // Disable atom garbage collection
  YAP_SetYAPFlag(YAPC_ENABLE_AGC, 0);
  // Now YAP Prolog is available
  const auto functor_atom_strs = sexpr_parser::CollectFunctorAtoms(nodes);
  const auto non_functor_atom_strs = sexpr_parser::CollectNonFunctorAtoms(nodes);
  ConstructAtomDictionary(functor_atom_strs, non_functor_atom_strs);
  atom_to_functor_arity.clear();
  for (const auto& pair : functor_atom_strs) {
    atom_to_functor_arity.emplace(StringToAtom(pair.first), pair.second);
  }
  CacheGoalValues(non_functor_atom_strs);
  CacheConstantYapObjects();
  CacheRoles();
  CacheInitialFacts();
  CachePossibleFacts();
  CachePossibleActions();
  DetectOrderedDomains();
  DetectStepCounters();
  DetectFactActionConnections();
//  DetectFactOrderedArgs();
//  DetectActionOrderedArgs();
}

namespace {

std::string LoadStringFromFile(const std::string& filename) {
  std::ifstream ifs(filename);
  if (!ifs) {
    throw std::runtime_error("File '" + filename + "' does not exists.");
  }
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

}

void InitializeFromFile(const std::string& kif_filename) {
  boost::filesystem::path path(kif_filename);
  Initialize(LoadStringFromFile(kif_filename), path.stem().string());
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

State::State() : facts_(initial_facts), legal_actions_(0), is_terminal_(false), goals_(0), joint_action_history_(0) {
}

State::State(const std::vector<Tuple>& facts, const std::vector<JointAction>& joint_action_history) : facts_(facts), legal_actions_(0), is_terminal_(false), goals_(0), joint_action_history_(joint_action_history) {
}

State::State(const std::vector<Tuple>& facts, const std::vector<int>& goals, const std::vector<JointAction>& joint_action_history) : facts_(facts), legal_actions_(0), is_terminal_(!goals.empty()), goals_(goals), joint_action_history_(joint_action_history) {
}

State::State(const State& another) : facts_(another.facts_), legal_actions_(another.legal_actions_), is_terminal_(another.is_terminal_), goals_(another.goals_), joint_action_history_(another.joint_action_history_) {
}

const FactSet& State::GetFacts() const {
  return facts_;
}

std::string State::ToString() const {
  std::ostringstream o;
  for (const auto& fact : facts_) {
    o << TupleToString(fact) << std::endl;
  }
  return o.str();
}

const std::vector<std::vector<Tuple>>& State::GetLegalActions() const {
  if (!legal_actions_.empty()) {
    return legal_actions_;
  }
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  const auto fact_term = TuplesToYapPairTerm(facts_);
  std::array<YAP_Term, 2> args = {{ fact_term, YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_legal_functor, 2, args.data());
  auto slot = YAP_InitSlot(goal);
#ifdef NDEBUG
  YAP_RunGoalOnce(goal);
#else
  auto okay = YAP_RunGoalOnce(goal);
  assert(okay && "Every role must always have at least one legal action.");
#endif
  const auto result = YAP_GetFromSlot(slot);
  const auto role_actions_pairs_term = YAP_ArgOfTerm(2, result);
  legal_actions_ = YapPairTermToActions(role_actions_pairs_term);
  YAP_Reset();
  YAP_RecoverSlots(1);
  return legal_actions_;
}

State State::GetNextState(const JointAction& joint_action) const {
  if (next_state_caching_enabled) {
    auto next_facts_cache = next_facts_.find(joint_action);
    if (next_facts_cache != next_facts_.end()) {
      auto next_joint_action_history = joint_action_history_;
      next_joint_action_history.push_back(joint_action);
      return State(next_facts_cache->second.first, next_facts_cache->second.second, next_joint_action_history);
    }
  }
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  std::array<YAP_Term, 4> args = {{ TuplesToYapPairTerm(facts_), JointActionToYapPairTerm(joint_action), YAP_MkVarTerm(), YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_next_and_goal_functor, 4, args.data());
  auto slot = YAP_InitSlot(goal);
#ifdef NDEBUG
  YAP_RunGoalOnce(goal);
#else
  auto okay = YAP_RunGoalOnce(goal);
#endif
  assert(okay);
  const auto result = YAP_GetFromSlot(slot);
  const auto facts_term = YAP_ArgOfTerm(3, result);
  const auto goal_term = YAP_ArgOfTerm(4, result);
  if (next_state_caching_enabled) {
    const auto pos = next_facts_.emplace(joint_action, std::make_pair(YapPairTermToTuples(facts_term), YapPairTermToGoals(goal_term)));
    assert(pos.second);
    YAP_Reset();
    YAP_RecoverSlots(1);
    auto next_joint_action_history = joint_action_history_;
    next_joint_action_history.push_back(joint_action);
    return State(pos.first->second.first, pos.first->second.second, next_joint_action_history);
  } else {
    auto next_joint_action_history = joint_action_history_;
    next_joint_action_history.push_back(joint_action);
    State next_state(YapPairTermToTuples(facts_term), YapPairTermToGoals(goal_term), next_joint_action_history);
    YAP_Reset();
    YAP_RecoverSlots(1);
    return next_state;
  }
}

bool State::IsTerminal() const {
  return is_terminal_;
}

const std::vector<int>& State::GetGoals() const {
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
  auto slot = YAP_InitSlot(goal);
#ifdef NDEBUG
  YAP_RunGoalOnce(goal);
#else
  auto okay = YAP_RunGoalOnce(goal);
  assert(okay);
#endif
  const auto result = YAP_GetFromSlot(slot);
  const auto role_goal_pairs_term = YAP_ArgOfTerm(2, result);
  goals_ = YapPairTermToGoals(role_goal_pairs_term);
  assert(!goals_.empty());
  YAP_Reset();
  YAP_RecoverSlots(1);
  return goals_;
}

std::vector<int> State::Simulate() const {
#ifndef GGPE_SINGLE_THREAD
  std::lock_guard<Mutex> lk(mutex);
#endif
  const auto fact_term = TuplesToYapPairTerm(facts_);
  std::array<YAP_Term, 2> args = {{ fact_term, YAP_MkVarTerm() }};
  auto goal = YAP_MkApplTerm(state_simulate_functor, 2, args.data());
  auto slot = YAP_InitSlot(goal);
#ifdef NDEBUG
  YAP_RunGoalOnce(goal);
#else
  auto okay = YAP_RunGoalOnce(goal);
  assert(okay && "Simulation must always succeed.");
#endif
  const auto result = YAP_GetFromSlot(slot);
  const auto role_goal_pairs_term = YAP_ArgOfTerm(2, result);
  const auto goals = YapPairTermToGoals(role_goal_pairs_term);
  YAP_Reset();
  YAP_RecoverSlots(1);
  return goals;
}

bool State::operator==(const State& another) const {
  return facts_ == another.facts_;
}

bool State::operator!=(const State& another) const {
  return facts_ != another.facts_;
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

void SetNextStateCachingEnabled(const bool enabled) {
  next_state_caching_enabled = enabled;
}

bool IsNextStateCachingEnabled() {
  return next_state_caching_enabled;
}

void InitializeTicTacToe() {
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
  Initialize(tictactoe_kif, "tictactoe");
}

const std::string& GetGameName() {
  return game_name;
}

}
