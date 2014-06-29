#include "gtest/gtest.h"
#include "sexpr_parser.hpp"
#include "file_utils.hpp"

#include <algorithm>

namespace sexpr_parser {

namespace {

const auto kTicTacToeKIF = file_utils::LoadStringFromFile("kif/tictactoe.kif");
const auto kBreakthroughKIF =
    file_utils::LoadStringFromFile("kif/breakthrough.kif");

}

TEST(RemoveComments, Test) {
  ASSERT_TRUE(RemoveComments("; comment\n a ; comment") == "\n a ");
}

TEST(Parse, Empty) {
  ASSERT_TRUE(Parse("").empty());
  ASSERT_TRUE(Parse(" \n\t").empty());
  ASSERT_TRUE(Parse("  \n\n\t\t").empty());
  ASSERT_TRUE(Parse(" \n\t \n\t").empty());
}

TEST(Parse, SingleLiteral) {
  const auto nodes = Parse("a");
  ASSERT_TRUE(nodes.size() == 1);
  const auto node = nodes.front();
  ASSERT_TRUE(node.IsLeaf());
  ASSERT_TRUE(node.GetValue() == "a");
}

TEST(Parse, EmptyParen) {
  const auto nodes = Parse("()");
  ASSERT_TRUE(nodes.size() == 1);
  const auto node = nodes.front();
  ASSERT_TRUE(!node.IsLeaf());
  ASSERT_TRUE(node.GetChildren().empty());
}

TEST(Parse, LowerReservedWords) {
  const auto str = std::string("(ROLE INIT TRUE DOES LEGAL NEXT TERMINAL GOAL BASE INPUT OR NOT DISTINCT NOT_RESERVED)");
  const auto answer = std::string("(role init true does legal next terminal goal base input or not distinct NOT_RESERVED)");
  const auto nodes = Parse(str);
  ASSERT_TRUE(nodes.size() == 1);
  const auto node = nodes.front();
  ASSERT_TRUE(node.ToSexpr() == answer);
}

TEST(Parse, Reparse) {
  const auto nodes = Parse("(a (b (c) d) e)");
  ASSERT_TRUE(nodes.size() == 1);
  const auto node = nodes.front();
  const auto sexpr = node.ToSexpr();
  const auto another_nodes = Parse(sexpr);
  ASSERT_TRUE(std::equal(nodes.begin(), nodes.end(), another_nodes.begin()));
}

TEST(Parse, FlattenTupleWithOneChild) {
  const auto kif = std::string("(((a)) (b (c) d) e)");
  const auto kif_flattened = std::string("(a (b c d) e)");
  const auto nodes = Parse(kif, true);
  const auto nodes_flattened = Parse(kif_flattened, true);
  ASSERT_TRUE(nodes.size() == 1);
  ASSERT_TRUE(nodes_flattened.size() == 1);
  ASSERT_TRUE(std::equal(nodes.begin(), nodes.end(), nodes_flattened.begin()));
}

TEST(Parse, ToPrologClause) {
  const auto nodes = Parse("(role player) fact1 (fact2 1) (<= rule1 fact1) (<= (rule2 ?x) fact1 (fact2 ?x))");
  ASSERT_TRUE(nodes.size() == 5);
  ASSERT_TRUE(nodes[0].ToPrologClause(false, "", "") == "role(player).");
  ASSERT_TRUE(nodes[1].ToPrologClause(false, "", "") == "fact1.");
  ASSERT_TRUE(nodes[2].ToPrologClause(false, "", "") == "fact2(1).");
  ASSERT_TRUE(nodes[3].ToPrologClause(false, "", "") == "rule1 :- fact1.");
  ASSERT_TRUE(nodes[4].ToPrologClause(false, "", "") == "rule2(_x) :- fact1, fact2(_x).");
}

TEST(Parse, ToProlog) {
  const auto nodes = Parse("(role player) fact1 (fact2 1) (<= rule1 fact1) (<= (rule2 ?x) fact1 (fact2 ?x))");
  const std::string answer =
      "role(player).\n"
      "fact1.\n"
      "fact2(1).\n"
      "rule1 :- fact1.\n"
      "rule2(_x) :- fact1, fact2(_x).\n";
  const std::string answer_quoted =
      "'role'('player').\n"
      "'fact1'.\n"
      "'fact2'('1').\n"
      "'rule1' :- 'fact1'.\n"
      "'rule2'(_x) :- 'fact1', 'fact2'(_x).\n";
  ASSERT_EQ(ToProlog(nodes, false), answer);
  ASSERT_EQ(ToProlog(nodes, true), answer_quoted);
}

TEST(Parse, FilterVariableCode) {
  const auto& nodes = Parse("(<= head (body ?v+v))");
  const std::string answer = "head :- body(_v_c43_v).\n";
  ASSERT_EQ(ToProlog(nodes, false), answer);
}

TEST(CollectAtoms, Test) {
  const auto nodes = Parse("(role player) fact1 (fact2 1) (<= rule1 fact1) (<= (rule2 ?x) fact1 (fact2 ?x))");
  const auto atoms = CollectAtoms(nodes);
  ASSERT_TRUE(atoms.size() == 7); // role, player, fact1, fact2, 1, rule1, rule2
  ASSERT_TRUE(atoms.count("role"));
  ASSERT_TRUE(atoms.count("player"));
  ASSERT_TRUE(atoms.count("fact1"));
  ASSERT_TRUE(atoms.count("fact2"));
  ASSERT_TRUE(atoms.count("1"));
  ASSERT_TRUE(atoms.count("rule1"));
  ASSERT_TRUE(atoms.count("rule2"));
  ASSERT_TRUE(!atoms.count("?x"));
  ASSERT_TRUE(!atoms.count("<="));
}

TEST(CollectNonFunctorAtoms, Test) {
  const auto nodes = Parse("(role player) fact1 (fact2 1) (<= rule1 fact1) (<= (rule2 ?x) fact1 (fact2 ?x))");
  const auto atoms = CollectNonFunctorAtoms(nodes);
  ASSERT_TRUE(atoms.size() == 4); // player, fact1, 1, rule1
  ASSERT_TRUE(!atoms.count("role"));
  ASSERT_TRUE(atoms.count("player"));
  ASSERT_TRUE(atoms.count("fact1"));
  ASSERT_TRUE(!atoms.count("fact2"));
  ASSERT_TRUE(atoms.count("1"));
  ASSERT_TRUE(atoms.count("rule1"));
  ASSERT_TRUE(!atoms.count("rule2"));
  ASSERT_TRUE(!atoms.count("?x"));
  ASSERT_TRUE(!atoms.count("<="));
}

TEST(CollectFunctorAtoms, Test) {
  const auto nodes = Parse("(role player) fact1 (fact2 1) (<= rule1 fact1) (<= (rule2 ?x) fact1 (fact2 ?x))");
  const auto atoms = CollectFunctorAtoms(nodes);
  ASSERT_TRUE(atoms.size() == 3); // role, fact2, rule2
  ASSERT_TRUE(atoms.count("role"));
  ASSERT_TRUE(atoms.at("role") == 1);
  ASSERT_TRUE(!atoms.count("player"));
  ASSERT_TRUE(!atoms.count("fact1"));
  ASSERT_TRUE(atoms.count("fact2"));
  ASSERT_TRUE(atoms.at("fact2") == 1);
  ASSERT_TRUE(!atoms.count("1"));
  ASSERT_TRUE(!atoms.count("rule1"));
  ASSERT_TRUE(atoms.count("rule2"));
  ASSERT_TRUE(atoms.at("rule2") == 1);
  ASSERT_TRUE(!atoms.count("?x"));
  ASSERT_TRUE(!atoms.count("<="));
}

TEST(ReplaceAtoms, Test) {
  const auto nodes = Parse("(role player) fact1 (fact2 1) (<= rule1 fact1) (<= (rule2 ?x) fact1 (fact2 ?x))");
  const auto nodes_replaced = ReplaceAtoms(nodes, "fact1", "fact3");
  ASSERT_TRUE(nodes_replaced.size() == 5);
  ASSERT_TRUE(nodes_replaced[0].ToPrologClause(false, "", "") == "role(player).");
  ASSERT_TRUE(nodes_replaced[1].ToPrologClause(false, "", "") == "fact3.");
  ASSERT_TRUE(nodes_replaced[2].ToPrologClause(false, "", "") == "fact2(1).");
  ASSERT_TRUE(nodes_replaced[3].ToPrologClause(false, "", "") == "rule1 :- fact3.");
  ASSERT_TRUE(nodes_replaced[4].ToPrologClause(false, "", "") == "rule2(_x) :- fact3, fact2(_x).");
}

TEST(CollectDynamicRelations, Reserved) {
  const auto nodes = Parse("");
  const auto dynamic_relations = CollectDynamicRelations(nodes);
  ASSERT_TRUE(dynamic_relations.count("true"));
  ASSERT_TRUE(dynamic_relations.count("does"));
  ASSERT_TRUE(dynamic_relations.count("legal"));
  ASSERT_TRUE(dynamic_relations.count("next"));
  ASSERT_TRUE(dynamic_relations.count("terminal"));
  ASSERT_TRUE(dynamic_relations.count("goal"));
  ASSERT_FALSE(dynamic_relations.count("role"));
  ASSERT_FALSE(dynamic_relations.count("init"));
  ASSERT_FALSE(dynamic_relations.count("base"));
  ASSERT_FALSE(dynamic_relations.count("input"));
  // Whether 'or', 'not' and 'distinct' are dynamic or not is game-dependent.
}

TEST(CollectDynamicRelations, DirectlyDependent) {
  const auto nodes = Parse("(<= a (true fact)) (<= b (not (true fact)))");
  const auto dynamic_relations = CollectDynamicRelations(nodes);
  ASSERT_TRUE(dynamic_relations.count("a"));
  ASSERT_TRUE(dynamic_relations.count("b"));
}

TEST(CollectDynamicRelations, IndirectlyDependent) {
  const auto nodes = Parse("(<= a (true fact)) (<= b a) (<= c b)");
  const auto dynamic_relations = CollectDynamicRelations(nodes);
  ASSERT_TRUE(dynamic_relations.count("b"));
  ASSERT_TRUE(dynamic_relations.count("c"));
}

TEST(CollectDynamicRelations, Independent) {
  const auto nodes = Parse("a (<= b a)");
  const auto dynamic_relations = CollectDynamicRelations(nodes);
  ASSERT_FALSE(dynamic_relations.count("a"));
  ASSERT_FALSE(dynamic_relations.count("b"));
}

TEST(CollectDynamicRelations, TicTacToe) {
  const auto nodes = Parse(kTicTacToeKIF);
  const auto dynamic_relations = CollectDynamicRelations(nodes);
  const std::unordered_set<std::string> answer = {
      "true",
      "does",
      "legal",
      "next",
      "terminal",
      "goal",
      "row",
      "column",
      "diagonal",
      "line",
      "open"
  };
  ASSERT_EQ(dynamic_relations, answer);
}

TEST(CollectStaticRelations, TicTacToe) {
  const auto nodes = Parse(kTicTacToeKIF);
  const auto static_relations = CollectStaticRelations(nodes);
  const std::unordered_map<std::string, int> answer = {
      {"index", 1},
//      {"cell", 3},
//      {"control", 1},
//      {"mark", 2},
  };
  ASSERT_EQ(static_relations, answer);
}

}
