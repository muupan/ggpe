#include "gtest/gtest.h"
#include "ggpe.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <thread>
#include <boost/timer/timer.hpp>

namespace ggpe {

namespace {

void SimpleSimulate(const StateSp& state) {
  auto tmp_state = state;
  while (!tmp_state->IsTerminal()) {
//    std::cout << "tmp_state:" << std::endl;
//    std::cout << tmp_state->ToString() << std::endl;
    JointAction joint_action(GetRoleCount());
    for (const auto role_idx : GetRoleIndices()) {
      ASSERT_EQ(tmp_state->GetLegalActions().size(), GetRoleCount());
      ASSERT_TRUE(!tmp_state->GetLegalActions().at(role_idx).empty());
      joint_action.at(role_idx) = tmp_state->GetLegalActions().at(role_idx).front();
    }
    tmp_state = tmp_state->GetNextState(joint_action);
  }
}

#ifndef __clang__
void CheckParallelizability(const int total_sim) {
  auto state = CreateInitialState();
  for (auto n = 1; n <= 4; ++n) {
    std::cout << "num_threads(" << n << "):" << std::endl;
    boost::timer::cpu_timer timer;
#pragma omp parallel for num_threads(n) schedule(dynamic)
    for (auto i = 0; i < total_sim; ++i) {
      SimpleSimulate(state);
    }
    std::cout << timer.format() << std::endl;
  }
}
#endif

const auto tictactoe_filename = "kif/tictactoe.kif";
const auto breakthrough_filename = "kif/breakthrough.kif";
const auto pilgrimage_filename = "kif/pilgrimage.kif";
const auto chinesecheckers4_filename = "kif/chinesecheckers4.kif";

void TestChineseCheckers4() {
  auto state = CreateInitialState();
  std::cout << state->ToString() << std::endl;
  ASSERT_FALSE(state->IsTerminal());
  ASSERT_EQ(GetRoleCount(), 4);
  ASSERT_EQ(StringToRoleIndex("yellow"), 0);
  ASSERT_EQ(StringToRoleIndex("green"), 1);
  ASSERT_EQ(StringToRoleIndex("blue"), 2);
  ASSERT_EQ(StringToRoleIndex("magenta"), 3);
  ASSERT_EQ(state->GetFacts().size(), 20);
//  ASSERT_EQ(state->GetLegalActions()[0].size(), 3);
//  ASSERT_EQ(state->GetLegalActions()[1].size(), 10);
  state->Simulate();
  SimpleSimulate(CreateInitialState());
}

}

TEST(GetGameName, TicTacToe) {
  InitializeTicTacToe();
  ASSERT_EQ(GetGameName(), "tictactoe");
}

TEST(Role, TicTacToe) {
  InitializeTicTacToe();
  ASSERT_EQ(GetRoleCount(), 2);
  ASSERT_EQ(GetRoleIndices().size(), 2);
  ASSERT_FALSE(IsValidRoleIndex(-1));
  ASSERT_TRUE(IsValidRoleIndex(0));
  ASSERT_TRUE(IsValidRoleIndex(1));
  ASSERT_FALSE(IsValidRoleIndex(2));
  ASSERT_EQ(StringToRoleIndex("white"), 0);
  ASSERT_EQ(StringToRoleIndex("black"), 1);
}

TEST(InitialState, TicTacToe) {
  InitializeTicTacToe();
  auto state = CreateInitialState();
  ASSERT_FALSE(state->IsTerminal());
  const auto& facts = state->GetFacts();
  ASSERT_EQ(facts.size(), 10);
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
    ASSERT_NE(std::find(facts.begin(), facts.end(), answer_fact), facts.end());
  }
}

TEST(GetLegalAction, TicTacToe) {
  InitializeTicTacToe();
  auto state = CreateInitialState();
  ASSERT_EQ(state->GetLegalActions().size(), 2);
  const auto actions_for_white = state->GetLegalActions().at(0);
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
  const auto actions_for_black = state->GetLegalActions().at(1);
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
  InitializeTicTacToe();
  auto state = CreateInitialState();
  JointAction joint_action({StringToTuple("(mark 2 2)"), StringToTuple("noop")});
  const auto next_state = state->GetNextState(joint_action);
  const auto next_facts = next_state->GetFacts();
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
  ASSERT_EQ(next_state->GetLegalActions().size(), 2);
  ASSERT_EQ(next_state->GetLegalActions().at(0).size(), 1);
  ASSERT_EQ(next_state->GetLegalActions().at(1).size(), 8);
}

TEST(Simulate, TicTacToe) {
  InitializeTicTacToe();
  auto state = CreateInitialState();
  state->Simulate();
  SimpleSimulate(CreateInitialState());
}

TEST(Atoms, TicTacToe) {
  InitializeTicTacToe();
  ASSERT_EQ(AtomToString(atoms::kFree), "?");
  ASSERT_EQ(AtomToString(atoms::kFree - 1), "?-1");
  ASSERT_EQ(AtomToString(atoms::kFree - 255), "?-255");
  ASSERT_EQ(AtomToString(atoms::kFree + 1), "?+1");
  ASSERT_EQ(AtomToString(atoms::kFree + 255), "?+255");
}

TEST(GetJointActionHistory, TicTacToe) {
  InitializeTicTacToe();
  // Initial state
  auto initial_state = CreateInitialState();
  ASSERT_EQ(initial_state->GetJointActionHistory().size(), 0);
  // Second state
  const auto first_action = JointAction({StringToTuple("(mark 1 1)"), StringToTuple("noop")});
  const auto second_state = initial_state->GetNextState(first_action);
  ASSERT_EQ(second_state->GetJointActionHistory().size(), 1);
  ASSERT_EQ(second_state->GetJointActionHistory().at(0), first_action);
  // Third state
  const auto second_action = JointAction({StringToTuple("noop"), StringToTuple("(mark 2 2)")});
  const auto third_state = second_state->GetNextState(second_action);
  ASSERT_EQ(third_state->GetJointActionHistory().size(), 2);
  ASSERT_EQ(third_state->GetJointActionHistory().at(0), first_action);
  ASSERT_EQ(third_state->GetJointActionHistory().at(1), second_action);
}

TEST(InitializeFromFile, Breakthrough) {
  InitializeFromFile(breakthrough_filename);
  auto state = CreateInitialState();
  ASSERT_FALSE(state->IsTerminal());
  ASSERT_TRUE(GetRoleCount() == 2);
  ASSERT_TRUE(StringToRoleIndex("white") == 0);
  ASSERT_TRUE(StringToRoleIndex("black") == 1);
  const auto& facts = state->GetFacts();
  ASSERT_TRUE(facts.size() == 33);
  state->GetLegalActions();
  state->Simulate();
  SimpleSimulate(CreateInitialState());
}

TEST(InitializeFromFile, Pilgrimage) {
  InitializeFromFile(pilgrimage_filename);
  auto state = CreateInitialState();
  std::cout << state->ToString() << std::endl;
  ASSERT_FALSE(state->IsTerminal());
  ASSERT_EQ(GetRoleCount(), 2);
  ASSERT_EQ(StringToRoleIndex("red"), 0);
  ASSERT_EQ(StringToRoleIndex("blue"), 1);
  ASSERT_EQ(state->GetFacts().size(), 9);
  ASSERT_EQ(state->GetLegalActions()[0].size(), 3);
  ASSERT_EQ(state->GetLegalActions()[1].size(), 10);
  state->Simulate();
  SimpleSimulate(CreateInitialState());
}

TEST(InitializeFromFile, ChineseCheckers4) {
  InitializeFromFile(chinesecheckers4_filename, EngineBackend::YAP);
  TestChineseCheckers4();
  InitializeFromFile(chinesecheckers4_filename, EngineBackend::GDLCC);
  TestChineseCheckers4();
}

#ifndef __clang__
TEST(CheckParallelizability, Breakthrough) {
  InitializeFromFile(breakthrough_filename, EngineBackend::YAP);
  CheckParallelizability(100);
  InitializeFromFile(breakthrough_filename, EngineBackend::GDLCC);
  CheckParallelizability(1000);
}

TEST(CheckParallelizability, ChineseCheckers4) {
  InitializeFromFile(chinesecheckers4_filename, EngineBackend::YAP);
  CheckParallelizability(100);
  InitializeFromFile(chinesecheckers4_filename, EngineBackend::GDLCC);
  CheckParallelizability(1000);
}
#endif

//TEST(ParallelSimulation, YAP) {
//  InitializeFromFile(breakthrough_filename, EngineBackend::YAP);
//  auto state = CreateInitialState();
//  for (auto n = 1; n <= 4; ++n) {
//    std::cout << "num_threads(" << n << "):" << std::endl;
//    boost::timer::cpu_timer timer;
//#pragma omp parallel for num_threads(n)
//    for (auto i = 0; i < 10; ++i) {
//      SimpleSimulate(state);
//    }
//    std::cout << timer.format() << std::endl;
//  }
//}

}
