#include "gtest/gtest.h"
#include "gdlcc_engine.hpp"

#include <iostream>
#include "file_utils.hpp"

namespace ggpe {
namespace gdlcc {

namespace {
const auto tictactoe_filename = "kif/tictactoe.kif";
const auto tictactoe_kif =
    file_utils::LoadStringFromFile(tictactoe_filename);
const auto breakthrough_filename = "kif/breakthrough.kif";
const auto breakthrough_kif =
    file_utils::LoadStringFromFile(breakthrough_filename);
}

TEST(GDLCCEngine, TicTacToe) {
  InitializeGDLCCEngine(tictactoe_kif, "tictactoe", false);
  const auto state = CreateInitialState();
  std::cout << state->ToString() << std::endl;
  const auto& legal_actions = state->GetLegalActions();
  ASSERT_EQ(legal_actions.size(), 2);
  ASSERT_EQ(legal_actions.at(0).size(), 9);
  ASSERT_EQ(legal_actions.at(1).size(), 1);
  const auto next_state =
      state->GetNextState(JointAction{{
          legal_actions.at(0).front(),
          legal_actions.at(1).front()}});
  std::cout << next_state->ToString() << std::endl;
  auto goals = next_state->Simulate();
  ASSERT_TRUE(!goals.empty());
}

TEST(GDLCCEngine, Breakthrough) {
  InitializeGDLCCEngine(breakthrough_kif, "breakthrough", false);
  auto state = CreateInitialState();
  std::cout << state->ToString() << std::endl;
  const auto& legal_actions = state->GetLegalActions();
  ASSERT_EQ(legal_actions.size(), 2);
  ASSERT_EQ(legal_actions.at(0).size(), 22);
  ASSERT_EQ(legal_actions.at(1).size(), 1);
  const auto next_state =
      state->GetNextState(JointAction{{
          legal_actions.at(0).front(),
          legal_actions.at(1).front()}});
  std::cout << next_state->ToString() << std::endl;
  auto goals = next_state->Simulate();
  ASSERT_TRUE(!goals.empty());
}

}
}
