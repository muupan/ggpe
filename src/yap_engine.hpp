#ifndef YAP_ENGINE_HPP_
#define YAP_ENGINE_HPP_

#include "ggpe.hpp"
#include "state.hpp"

namespace ggpe {
namespace yap {

class YapState : public State {
public:
  YapState() = delete;
  /**
   * Construct a state with a given set of facts
   */
  YapState(const FactSet& facts, const std::vector<JointAction>& joint_action_history);
  /**
   * Construct a state with a given set of facts, caching pre-computed goals
   */
  YapState(const FactSet& facts, const std::vector<int>& goals, const std::vector<JointAction>& joint_action_history);
  /**
   * Copy constructor
   */
  YapState(const YapState& another);
  /**
   * @return a set of facts
   */
  const FactSet& GetFacts() const override;
  /**
   * @return a set of legal actions for each role (results are cached)
   */
  virtual const std::vector<ActionSet>& GetLegalActions() const override;
  /**
   * @return the next state when performing a given joint action
   */
  virtual StateSp GetNextState(const JointAction& joint_action) const override;
  /**
   * @return true iif this state is terminal
   */
  virtual bool IsTerminal() const override;
  /**
   * @return a goal value for each role if defined, otherwise empty
   */
  virtual const std::vector<int>& GetGoals() const override;
  /**
   * @return resulting goals of a random simulation from this state
   */
  virtual std::vector<int> Simulate() const override;
  /**
   * @return joint action history from the initial state
   */
  const std::vector<JointAction>& GetJointActionHistory() const override;

  std::string ToString() const override;

private:
  FactSet facts_;
  mutable std::vector<ActionSet> legal_actions_;
  bool is_terminal_;
  mutable std::vector<int> goals_;
  mutable std::vector<JointAction> joint_action_history_;
};

void InitializeYapEngine(
    const std::string& kif,
    const std::string& name,
    const bool enables_tabling);

StateSp CreateInitialState();

}

}

#endif /* YAP_ENGINE_HPP_ */
