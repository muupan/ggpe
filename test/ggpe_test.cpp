#include "gtest/gtest.h"
#include "ggpe.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>

std::string LoadStringFromFile(const std::string& filename) {
  std::ifstream ifs(filename);
  assert(ifs);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

const auto kif_tictactoe = LoadStringFromFile("test/tictactoe.kif");
const auto kif_breakthrough = LoadStringFromFile("test/breakthrough.kif");

TEST(Initialize, TicTacToe) {
  ggpe::Initialize(kif_tictactoe);
  ggpe::State state;
  ASSERT_TRUE(ggpe::GetRoleCount() == 2);
  ASSERT_TRUE(ggpe::StringToRoleIndex("white") == 0);
  ASSERT_TRUE(ggpe::StringToRoleIndex("black") == 1);
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
    const auto answer_fact = ggpe::StringToTuple(answer_fact_str);
    ASSERT_TRUE(std::find(facts.begin(), facts.end(), answer_fact) != facts.end());
  }
  state.GetLegalActions();
  state.Simulate();
}

TEST(Initialize, Breakthrough) {
  ggpe::Initialize(kif_breakthrough);
  ggpe::State state;
  ASSERT_TRUE(ggpe::GetRoleCount() == 2);
  ASSERT_TRUE(ggpe::StringToRoleIndex("white") == 0);
  ASSERT_TRUE(ggpe::StringToRoleIndex("black") == 1);
  const auto& facts = state.GetFacts();
  ASSERT_TRUE(facts.size() == 33);
  state.GetLegalActions();
  state.Simulate();
}
