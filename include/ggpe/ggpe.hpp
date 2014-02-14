#ifndef _GGPE_H_
#define _GGPE_H_

#include <memory>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <boost/functional/hash.hpp>

#include <Yap/YapInterface.h>

namespace ggpe {

/**
 * Atom of GDL
 */
using Atom = YAP_Atom;
/**
 * Tuple of GDL
 */
using Tuple = std::vector<Atom>;
/**
 * Fact tuple
 */
using Fact = Tuple;
/**
 * Action tuple
 */
using Action = Tuple;
/**
 * Set of Fact (order is not guaranteed)
 */
using FactSet = std::vector<Fact>;
/**
 * Set of Action (order is not guaranteed)
 */
using ActionSet = std::vector<Action>;
/**
 * Joint action (= every role's action, order by role)
 */
using JointAction = std::vector<Action>;

/**
 * A game state with manipulation interface
 */
class State {
public:
  /**
   * Construct an initial state
   */
  State();
  /**
   * Construct a state with a given set of facts
   */
  State(const FactSet& facts);
  /**
   * Construct a state with a given set of facts, caching pre-computed goals
   */
  State(const FactSet& facts, const std::vector<int>& goals);
  /**
   * Copy constructor
   */
  State(const State& another);
  /**
   * @return a set of facts
   */
  const FactSet& GetFacts() const;
  /**
   * @return a set of legal actions for each role (results are cached)
   */
  const std::vector<ActionSet>& GetLegalActions() const;
  /**
   * @return the next state when performing a given joint action
   */
  State GetNextState(const JointAction& joint_action) const;
  /**
   * @return true iif this state is terminal
   */
  bool IsTerminal() const;
  /**
   * @return a goal value for each role if defined, otherwise empty
   */
  const std::vector<int>& GetGoals() const;
  /**
   * @return resulting goals of a random simulation from this state
   */
  std::vector<int> Simulate() const;
  /**
   * @return string representation (multi-line)
   */
  std::string ToString() const;
  bool operator==(const State& another) const;
private:
  FactSet facts_;
  mutable std::vector<ActionSet> legal_actions_;
  bool is_terminal_;
  mutable std::vector<int> goals_;
};

/**
 * A pair of state and joint action
 */
using StateAction = std::pair<State, JointAction>;

/**
 * Initialize GGP Engine with a given KIF string
 * This is needed before using other functionalities
 */
void Initialize(const std::string& kif);

/**
 * @return the number of roles in the game
 */
int GetRoleCount();

/**
 * @return an ordered set of role indices, facilitating iteration
 */
const std::vector<int>& GetRoleIndices();

/**
 * @return true iif a given role_idx is valid
 */
bool IsValidRoleIndex(const int role_idx);

/**
 * Convert: role index -> string representation
 * @return a string representation
 */
std::string RoleIndexToString(const int role_index);

/**
 * Convert: string representation -> role index
 * @return a role index
 */
int StringToRoleIndex(const std::string& role_str);

/**
 * Convert: string representation -> tuple
 */
Tuple StringToTuple(const std::string& str);

/**
 * Convert: tuple -> string representation
 */
std::string TupleToString(const Tuple& tuple);

/**
 * Convert: string representation -> atom
 */
Atom StringToAtom(const std::string& str);

/**
 * Convert: atom -> string representation
 */
std::string AtomToString(const Atom atom);

/**
 * Note: enabled only if 'base' relation is defined in GDL
 * @return all the possible facts
 */
const FactSet& GetPossibleFacts();

/**
 * Note: enabled only if 'input' relation is defined in GDL
 * @return all the possible actions for each role
 */
const std::vector<ActionSet>& GetPossibleActions();

/**
 * Convert: joint action -> string representation
 */
std::string JointActionToString(const JointAction& joint_action);

/**
 * @return atoms which are used as step counters
 */
const std::unordered_set<Atom>& GetStepCounters();

/**
 * @return detected fact-action connections per atom pair
 */
const std::unordered_map<std::pair<Atom, Atom>, std::vector<std::pair<Atom, std::pair<int, int>>>>& GetFactActionConnections();

/**
 * @return detected ordered domains per atom
 */
const std::unordered_map<Atom, std::unordered_map<Atom, int>>& GetOrderedDomains();

}

namespace std {

template <>
struct hash<ggpe::State> {
  size_t operator()(const ggpe::State& value) const {
    return boost::hash_value(value.GetFacts());
  }
};

}

#endif /* _GGPE_H_ */
