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

#include <boost/timer.hpp>
#include <boost/functional/hash.hpp>
#include <Yap/YapInterface.h>

#include "sexpr_parser.hpp"

namespace std
{
// Hasher for std::vector
template <class T>
struct hash<vector<T>> {
  size_t operator()(const vector<T>& value) const {
    return boost::hash_value(value);
  }
};
template <class T, class U>
struct hash<pair<T, U>> {
  size_t operator()(const pair<T, U>& value) const {
    return boost::hash_value(value);
  }
};
}

namespace ggpe {

const auto kFunctorPrefix = std::string("f_");
const auto kAtomPrefix = std::string("a_");

using Mutex = std::mutex;
Mutex mutex;

std::vector<YAP_Atom> roles;
std::vector<int> role_indices;
std::unordered_map<YAP_Atom, int> atom_to_role_index;
std::unordered_map<YAP_Atom, int> atom_to_functor_arity;
std::unordered_map<YAP_Atom, int> atom_to_goal_values;
std::unordered_map<std::string, int> functor_name_to_arity;
std::vector<Tuple> initial_facts;
std::vector<Tuple> possible_facts;
std::vector<std::vector<Tuple>> possible_actions;
std::unordered_map<YAP_Atom, std::unordered_map<YAP_Atom, int>> atom_to_ordered_domain;
std::unordered_set<YAP_Atom> step_counter_atoms;
std::unordered_map<std::pair<Atom, Atom>, std::vector<std::pair<Atom, std::pair<int, int>>>> fact_action_connections;
std::unordered_map<YAP_Atom, std::unordered_map<int, YAP_Atom>> fact_ordered_args;
std::unordered_map<YAP_Atom, std::unordered_map<int, YAP_Atom>> action_ordered_args;

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

std::string AtomToString(const YAP_Atom atom) {
  assert(kFunctorPrefix.size() == kAtomPrefix.size());
  return std::string(YAP_AtomName(atom)).substr(kFunctorPrefix.size());
}

YAP_Atom StringToAtom(const std::string& atom_str) {
  return YAP_LookupAtom((kAtomPrefix + atom_str).c_str());
}

YAP_Atom StringToFunctor(const std::string& functor_str) {
  return YAP_LookupAtom((kFunctorPrefix + functor_str).c_str());
}

std::string YapAtomTermToString(const YAP_Term& term) {
  assert(YAP_IsAtomTerm(term));
  return AtomToString(YAP_AtomOfTerm(term));
}

std::string YapTermToString(const YAP_Term& term);

std::string YapCompoundTermToString(const YAP_Term& term) {
  assert(YAP_IsApplTerm(term));
  const auto f = YAP_FunctorOfTerm(term);
  const auto n = static_cast<int>(YAP_ArityOfFunctor(f));
  std::ostringstream o;
  o << '(' << AtomToString(YAP_NameOfFunctor(f)) << ' ';
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
  tuple.push_back(YAP_NameOfFunctor(functor));
  for (auto i = 1; i <= static_cast<int>(arity); ++i) {
    const auto arg = YAP_ArgOfTerm(i, term);
    assert(YAP_IsAtomTerm(arg) || YAP_IsApplTerm(arg));
    if (YAP_IsAtomTerm(arg)) {
      tuple.push_back(YAP_AtomOfTerm(arg));
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
    return Tuple({YAP_AtomOfTerm(term)});
  } else {
    return YapCompoundTermToTuple(term);
  }
}

template <class Iterator>
YAP_Term TupleToYapTerm(Iterator& pos) {
  if (!atom_to_functor_arity.count(*pos)) {
    return YAP_MkAtomTerm(*(pos++));
  }
  const auto arity = atom_to_functor_arity[*pos];
  const auto functor = YAP_MkFunctor(*pos, arity);
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
    const auto role_atom = YAP_AtomOfTerm(role_term);
    const auto action_term = YAP_HeadOfTerm(YAP_TailOfTerm(pair));
    assert(YAP_IsPairTerm(action_term));
    const auto role_index = atom_to_role_index[role_atom];
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
    const auto role_atom = YAP_AtomOfTerm(role_term);
    const auto goal_term = YAP_HeadOfTerm(YAP_TailOfTerm(pair));
    assert(YAP_IsAtomTerm(goal_term));
    const auto goal_atom = YAP_AtomOfTerm(goal_term);
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

std::vector<YAP_Atom> YapPairTermToAtoms(const YAP_Term pair_term) {
  assert(YAP_IsPairTerm(pair_term) || pair_term == empty_list_term);
  std::vector<YAP_Atom> atoms;
  auto temp_term = pair_term;
  while (YAP_IsPairTerm(temp_term)) {
    const auto head = YAP_HeadOfTerm(temp_term);
    assert(YAP_IsAtomTerm(head));
    atoms.push_back(YAP_AtomOfTerm(head));
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
  for (auto role_id = 0; role_id < static_cast<int>(roles.size()); ++role_id) {
    const auto role_action_pair = YapTermsToYapPairTerm(YAP_MkAtomTerm(roles[role_id]), TupleToYapTerm(joint_action[role_id]));
    temp = YAP_MkPairTerm(role_action_pair, temp);
  }
  return temp;
}

std::string YapAtomsToString(const std::vector<YAP_Atom>& atoms) {
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
    tuple.push_back(StringToFunctor(node.GetChildren().front().GetValue()));
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
    const auto role_atom = YAP_AtomOfTerm(role_term);
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
    std::cout << "Step counters: " << YapAtomsToString(atoms) << std::endl;
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
      const auto relation_atom = YAP_AtomOfTerm(relation_atom_term);
      // Domain atoms
      const auto domain_term = terms.back();
      assert(YAP_IsPairTerm(domain_term));
      const auto domain_atoms = YapPairTermToAtoms(domain_term);
      std::cout << "Domain by " << AtomToString(relation_atom) << ": " << YapAtomsToString(domain_atoms) << std::endl;
      std::unordered_map<YAP_Atom, int> domain_map;
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
      const auto fact_atom = YAP_AtomOfTerm(fact_atom_term);
      // Action atom
      const auto& action_atom_term = connection_term_terms[1];
      assert(YAP_IsAtomTerm(action_atom_term));
      const auto action_atom = YAP_AtomOfTerm(action_atom_term);
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
        const auto order_relation_atom = YAP_AtomOfTerm(order_relation_atom_term);
      // Args atom
        // Arg pair term
        const auto& arg_pair_term = terms[1];
        const auto& pair = YapPairTermToIntPair(arg_pair_term);
        args.push_back(std::make_pair(order_relation_atom, pair));
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
      const auto fact_rel_atom = YAP_AtomOfTerm(fact_rel_term);
      const auto& ordered_args_term = fact_term_and_ordered_args_term.at(1);
      const auto ordered_arg_terms = YapPairTermToYapTerms(ordered_args_term);
      std::unordered_map<int, YAP_Atom> arg_order_rel_map;
      for (const auto& ordered_arg_term : ordered_arg_terms) {
        const auto arg_term_and_order_rel_term = YapPairTermToYapTerms(ordered_arg_term);
        assert(arg_term_and_order_rel_term.size() == 2);
        const auto& arg_term = arg_term_and_order_rel_term.at(0);
        assert(YAP_IsIntTerm(arg_term));
        const auto arg = YAP_IntOfTerm(arg_term);
        const auto& order_rel_term = arg_term_and_order_rel_term.at(1);
        assert(YAP_IsAtomTerm(order_rel_term));
        const auto order_rel_atom = YAP_AtomOfTerm(order_rel_term);
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
      const auto action_rel_atom = YAP_AtomOfTerm(action_rel_term);
      const auto& ordered_args_term = action_term_and_ordered_args_term.at(1);
      const auto ordered_arg_terms = YapPairTermToYapTerms(ordered_args_term);
      std::unordered_map<int, YAP_Atom> arg_order_rel_map;
      for (const auto& ordered_arg_term : ordered_arg_terms) {
        const auto arg_term_and_order_rel_term = YapPairTermToYapTerms(ordered_arg_term);
        assert(arg_term_and_order_rel_term.size() == 2);
        const auto& arg_term = arg_term_and_order_rel_term.at(0);
        assert(YAP_IsIntTerm(arg_term));
        const auto arg = YAP_IntOfTerm(arg_term);
        const auto& order_rel_term = arg_term_and_order_rel_term.at(1);
        assert(YAP_IsAtomTerm(order_rel_term));
        const auto order_rel_atom = YAP_AtomOfTerm(order_rel_term);
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
void Initialize(const std::string& kif) {
  assert(!kif.empty());
  const auto nodes = sexpr_parser::ParseKIF(kif);
  assert(!nodes.empty());
  std::ofstream ofs("test.pl");
  ofs << sexpr_parser::ToProlog(nodes, true, kFunctorPrefix, kAtomPrefix, true);
  ofs.close();
  std::system("yap -g \"compile(['test.pl', 'interface.pl']), save_program('test.yap'), halt.\"");
  YAP_FastInit("test.yap");
  // Disable atom garbage collection
  YAP_SetYAPFlag(YAPC_ENABLE_AGC, 0);
  const auto functor_atom_strs = sexpr_parser::CollectFunctorAtoms(nodes);
  atom_to_functor_arity.clear();
  for (const auto& pair : functor_atom_strs) {
    atom_to_functor_arity.emplace(StringToFunctor(pair.first), pair.second);
  }
  const auto& non_functor_atom_strs = sexpr_parser::CollectNonFunctorAtoms(nodes);
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
  assert(ifs);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

}

void InitializeFromFile(const std::string& kif_filename) {
  Initialize(LoadStringFromFile(kif_filename));
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

State::State() : facts_(initial_facts), legal_actions_(0), is_terminal_(false), goals_(0) {
}

State::State(const std::vector<Tuple>& facts) : facts_(facts), legal_actions_(0), is_terminal_(false), goals_(0) {
}

State::State(const std::vector<Tuple>& facts, const std::vector<int>& goals) : facts_(facts), legal_actions_(0), is_terminal_(!goals.empty()), goals_(goals) {
}

State::State(const State& another) : facts_(another.facts_), legal_actions_(another.legal_actions_), is_terminal_(another.is_terminal_), goals_(another.goals_) {
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
  std::lock_guard<Mutex> lk(mutex);
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

State State::GetNextState(const std::vector<Tuple>& actions) const {
  std::lock_guard<Mutex> lk(mutex);
  std::array<YAP_Term, 4> args = {{ TuplesToYapPairTerm(facts_), JointActionToYapPairTerm(actions), YAP_MkVarTerm(), YAP_MkVarTerm() }};
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
  State next_state(YapPairTermToTuples(facts_term), YapPairTermToGoals(goal_term));
  YAP_Reset();
  YAP_RecoverSlots(1);
  return next_state;
}

bool State::IsTerminal() const {
  return is_terminal_;
}

const std::vector<int>& State::GetGoals() const {
  assert(is_terminal_);
  if (!goals_.empty()) {
    return goals_;
  }
  std::lock_guard<Mutex> lk(mutex);
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
  std::lock_guard<Mutex> lk(mutex);
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

const std::unordered_map<std::pair<Atom, Atom>, std::vector<std::pair<Atom, std::pair<int, int>>>>& GetFactActionConnections() {
  return fact_action_connections;
}

const std::unordered_map<Atom, std::unordered_map<Atom, int>>& GetOrderedDomains() {
  return atom_to_ordered_domain;
}

}
