#ifndef STATE_HPP_
#define STATE_HPP_

#include "ggpe.hpp"

namespace ggpe {

/**
 * A game state with manipulation interface
 */
class State {
public:
  /**
   * @return a set of facts
   */
  virtual const FactSet& GetFacts() const = 0;
  /**
   * @return a set of legal actions for each role (results are cached)
   */
  virtual const std::vector<ActionSet>& GetLegalActions() const = 0;
  /**
   * @return the next state when performing a given joint action
   */
  virtual StateSp GetNextState(const JointAction& joint_action) const = 0;
  /**
   * @return true iif this state is terminal
   */
  virtual bool IsTerminal() const = 0;
  /**
   * @return a goal value for each role if defined, otherwise empty
   */
  virtual const std::vector<int>& GetGoals() const = 0;
  /**
   * @return resulting goals of a random simulation from this state
   */
  virtual std::vector<int> Simulate() const = 0;
  /**
   * @return joint action history from the initial state
   */
  virtual const std::vector<JointAction>& GetJointActionHistory() const = 0;
  /**
   * @return a string representation
   */
  virtual std::string ToString() const = 0;
  /**
   * @return true if two states are the same
   */
  bool operator==(const State& another) const {
    return GetFacts() == another.GetFacts();
  }

  virtual ~State() {}
};

}

#endif /* STATE_HPP_ */
