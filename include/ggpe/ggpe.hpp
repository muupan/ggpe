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

namespace atoms {

constexpr auto kFree = 0;

}

/**
 * Atom of GDL
 */
using Atom = int;
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
using Goals = std::vector<int>;

class State;
using StateSp = std::shared_ptr<State>;

/**
 * A pair of state and joint action
 */
using StateAction = std::pair<StateSp, JointAction>;

enum class EngineBackend {
  YAP, GDLCC
};

/**
 * Initialize GGP Engine with a given KIF string
 * This is needed before using other functionalities
 */
void Initialize(
    const std::string& kif,
    const std::string& name="tmp",
    const EngineBackend backend=EngineBackend::YAP,
    const bool enables_tabling=false);
void InitializeFromFile(
    const std::string& kif_filename,
    const EngineBackend backend=EngineBackend::YAP,
    const bool enables_tabling=false);

/**
 * Initialize GGP Engine with TicTacToe KIF string.
 * This is intended for testing functionalities.
 */
void InitializeTicTacToe(const EngineBackend backend=EngineBackend::YAP);

const std::string& GetGameName();

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
const std::string& AtomToString(const Atom atom);

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
using AtomPair = std::pair<Atom, Atom>;
const std::unordered_map<AtomPair, std::vector<std::pair<Atom, std::pair<int, int>>>, boost::hash<AtomPair>>& GetFactActionConnections();

/**
 * @return detected ordered domains per atom
 */
const std::unordered_map<Atom, std::unordered_map<Atom, int>>& GetOrderedDomains();

StateSp CreateInitialState();

/**
 * Get the path where GGPE was compiled
 * @return
 */
std::string GetGGPEPath();

}

#include "state.hpp"

//namespace std {
//
//template <>
//struct hash<ggpe::State> {
//  size_t operator()(const ggpe::State& value) const {
//    return boost::hash_value(value.GetFacts());
//  }
//};
//
//}

#endif /* _GGPE_H_ */
