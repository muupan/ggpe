#include "gtest/gtest.h"
#include "ggpe.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>


namespace ggpe {

namespace {

std::string LoadStringFromFile(const std::string& filename) {
  std::ifstream ifs(filename);
  assert(ifs);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

void SimpleSimulate(const State& state) {
  auto tmp_state = state;
  while (!tmp_state.IsTerminal()) {
    JointAction joint_action(GetRoleCount());
    for (const auto role_idx : GetRoleIndices()) {
      joint_action.at(role_idx) = tmp_state.GetLegalActions().at(role_idx).front();
    }
    tmp_state = tmp_state.GetNextState(joint_action);
  }
}

const auto tictactoe_filename = "test/tictactoe.kif";
const auto breakthrough_filename = "test/breakthrough.kif";

}

TEST(Role, TicTacToe) {
  InitializeFromFile(tictactoe_filename);
  ASSERT_TRUE(GetRoleCount() == 2);
  ASSERT_TRUE(GetRoleIndices().size() == 2);
  ASSERT_FALSE(IsValidRoleIndex(-1));
  ASSERT_TRUE(IsValidRoleIndex(0));
  ASSERT_TRUE(IsValidRoleIndex(1));
  ASSERT_FALSE(IsValidRoleIndex(2));
  ASSERT_TRUE(StringToRoleIndex("white") == 0);
  ASSERT_TRUE(StringToRoleIndex("black") == 1);
}

TEST(InitialState, TicTacToe) {
  InitializeFromFile(tictactoe_filename);
  State state;
  ASSERT_FALSE(state.IsTerminal());
  const auto& facts = state.GetFacts();
  ASSERT_TRUE(facts.size() == 10);
  const auto answer_fact_strs = std::vector<std::string>({
    "(cell 1 1 b)",
    "(cell 1 2 b)",
    "(cell 1 3 b)",
    "(cell 2 1 b)",
    "(cell 2 2 b)",
    "(cell 2 3 b)",
    "(cell 3 1 b)",
    "(cell 3 2 b)",
    "(cell 3 3 b)",
    "(control white)"
  });
  for (const auto& answer_fact_str : answer_fact_strs) {
    const auto answer_fact = StringToTuple(answer_fact_str);
    ASSERT_TRUE(std::find(facts.begin(), facts.end(), answer_fact) != facts.end());
  }
}

TEST(GetLegalAction, TicTacToe) {
  InitializeFromFile(tictactoe_filename);
  State state;
  ASSERT_EQ(state.GetLegalActions().size(), 2);
  const auto actions_for_white = state.GetLegalActions().at(0);
  ASSERT_EQ(actions_for_white.size(), 9);
  const auto answer_action_strs_for_white = std::vector<std::string>({
    "(mark 1 1)",
    "(mark 1 2)",
    "(mark 1 3)",
    "(mark 2 1)",
    "(mark 2 2)",
    "(mark 2 3)",
    "(mark 3 1)",
    "(mark 3 2)",
    "(mark 3 3)",
  });
  for (const auto& answer_action_str : answer_action_strs_for_white) {
    const auto answer_fact = StringToTuple(answer_action_str);
    ASSERT_TRUE(std::find(actions_for_white.begin(), actions_for_white.end(), answer_fact) != actions_for_white.end());
  }
  const auto actions_for_black = state.GetLegalActions().at(1);
  ASSERT_EQ(actions_for_black.size(), 1);
  const auto answer_action_strs_for_black = std::vector<std::string>({
    "noop",
  });
  for (const auto& answer_action_str : answer_action_strs_for_black) {
    const auto answer_fact = StringToTuple(answer_action_str);
    ASSERT_TRUE(std::find(actions_for_black.begin(), actions_for_black.end(), answer_fact) != actions_for_black.end());
  }
}

TEST(GetNextState, TicTacToe) {
  InitializeFromFile(tictactoe_filename);
  State state;
  JointAction joint_action({StringToTuple("(mark 2 2)"), StringToTuple("noop")});
  const auto next_state = state.GetNextState(joint_action);
  const auto next_facts = next_state.GetFacts();
  const auto next_answer_fact_strs = std::vector<std::string>({
    "(cell 1 1 b)",
    "(cell 1 2 b)",
    "(cell 1 3 b)",
    "(cell 2 1 b)",
    "(cell 2 2 x)",
    "(cell 2 3 b)",
    "(cell 3 1 b)",
    "(cell 3 2 b)",
    "(cell 3 3 b)",
    "(control black)"
  });
  for (const auto& answer_fact_str : next_answer_fact_strs) {
    const auto answer_fact = StringToTuple(answer_fact_str);
    ASSERT_TRUE(std::find(next_facts.begin(), next_facts.end(), answer_fact) != next_facts.end());
  }
  ASSERT_EQ(next_state.GetLegalActions().size(), 2);
  ASSERT_EQ(next_state.GetLegalActions().at(0).size(), 1);
  ASSERT_EQ(next_state.GetLegalActions().at(1).size(), 8);
}

TEST(Simulate, TicTacToe) {
  InitializeFromFile(tictactoe_filename);
  State state;
  state.Simulate();
  SetNextStateCachingEnabled(true);
  SimpleSimulate(State());
  SetNextStateCachingEnabled(false);
  SimpleSimulate(State());
}

TEST(InitializeFromFile, Breakthrough) {
  InitializeFromFile(breakthrough_filename);
  State state;
  ASSERT_FALSE(state.IsTerminal());
  ASSERT_TRUE(GetRoleCount() == 2);
  ASSERT_TRUE(StringToRoleIndex("white") == 0);
  ASSERT_TRUE(StringToRoleIndex("black") == 1);
  const auto& facts = state.GetFacts();
  ASSERT_TRUE(facts.size() == 33);
  state.GetLegalActions();
  state.Simulate();
  SetNextStateCachingEnabled(true);
  SimpleSimulate(State());
  SetNextStateCachingEnabled(false);
  SimpleSimulate(State());
}

}
