#include "sexpr_parser.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <locale>
#include <sstream>

#include <boost/format.hpp>
#include <boost/functional/hash.hpp>
#include <boost/regex.hpp>
#include <boost/tokenizer.hpp>

namespace std
{
template <class T, class U>
struct hash<pair<T, U>> {
  size_t operator()(const pair<T, U>& a) const {
    return boost::hash_value(a);
  }
};
}

namespace sexpr_parser {

typedef boost::char_separator<char> Separator;
typedef boost::tokenizer<Separator> Tokenizer;

namespace {

const std::unordered_set<std::string> kReservedRelations = {
  "role",
  "init",
  "true",
  "does",
  "legal",
  "next",
  "goal",
  "terminal",
  "input",
  "base",
  "or",
  "not",
  "distinct"
};

/**
 * These relations is dynamic.
 * true and does: asserted and retracted by users dynamically.
 * legal, next, terminal and goal: depends on true and/or does.
 */
const std::unordered_set<std::string> kReservedDynamicRelations = {
    "true",
    "does",
    "legal",
    "next",
    "terminal",
    "goal"
};

std::string ToLowerCase(const std::string& str) {
  auto lowered = str;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
  return lowered;
}

/**
 * If a given string is a reserved, return it in lower cases. Otherwise,
 * return it as it is.
 * @param word
 * @return
 */
std::string LowerReservedWords(const std::string& word) {
  const auto lowered = ToLowerCase(word);
  if (kReservedRelations.count(lowered)) {
    return lowered;
  } else {
    return word;
  }
}

}

TreeNode::TreeNode(const std::string& value) :
    is_leaf_(true), value_(LowerReservedWords(value)), children_() {
}

TreeNode::TreeNode(const std::vector<TreeNode>& children) :
    is_leaf_(false), value_(), children_(children) {
}

bool TreeNode::IsLeaf() const {
  return is_leaf_;
}

bool TreeNode::IsVariable() const {
  return is_leaf_ && !value_.empty() && value_.front() == '?';
}

const std::string& TreeNode::GetValue() const {
  return value_;
}

const std::vector<TreeNode>& TreeNode::GetChildren() const {
  return children_;
}

std::string TreeNode::ToString() const {
  if (is_leaf_) {
    return "leaf:" + value_;
  } else {
    std::ostringstream o;
    o << "non-leaf[" << children_.size() << "](";
    for (const auto& child : children_) {
      o << ' ' << child.ToString();
    }
    o << " )";
    return o.str();
  }
}

std::string TreeNode::ToSexpr() const {
  if (is_leaf_) {
    return value_;
  } else {
    std::ostringstream o;
    o << '(';
    for (auto i = children_.begin(); i != children_.end(); ++i) {
      if (i != children_.begin()) {
        o << ' ';
      }
      o << i->ToSexpr();
    }
    o << ')';
    return o.str();
  }
}

std::string TreeNode::ChildrenToSexpr() const {
  std::ostringstream o;
  for (auto i = children_.begin(); i != children_.end(); ++i) {
    if (i != children_.begin()) {
      o << ' ';
    }
    o << i->ToSexpr();
  }
  return o.str();
}

std::string FilterVariableName(const std::string& base_name) {
  std::ostringstream o;
  for (auto i = base_name.begin(); i != base_name.end(); ++i) {
    if (std::isalnum(*i) || *i == '_') {
      o << *i;
    } else {
      // Symbols to char code
      o << "_c" << static_cast<int>(*i) << '_';
    }
  }
  return o.str();
}

std::string ConvertToPrologAtom(const std::string& value, const bool quotes_atoms, const std::string& atom_prefix) {
  auto atom = value;
  if (atom[0] == '?') {
    // Variable
    atom = '_' + FilterVariableName(atom.substr(1));
  } else {
    atom = atom_prefix + atom;
    if (quotes_atoms) {
      atom = '\'' + atom + '\'';
    }
  }
  return atom;
}

std::string TreeNode::ToPrologAtom(const bool quotes_atoms, const std::string& atom_prefix) const {
  assert(is_leaf_);
  return ConvertToPrologAtom(value_, quotes_atoms, atom_prefix);
}

std::string ConvertToPrologFunctor(const std::string& value, const bool quotes_atoms, const std::string& functor_prefix) {
  auto functor = functor_prefix + value;
  if (quotes_atoms) {
    functor = '\'' + functor + '\'';
  }
  return functor;
}

std::string TreeNode::ToPrologFunctor(const bool quotes_atoms, const std::string& functor_prefix) const {
  assert(is_leaf_);
  assert(value_[0] != '?');
  return ConvertToPrologFunctor(value_, quotes_atoms, functor_prefix);
}

std::string TreeNode::ToPrologTerm(const bool quotes_atoms, const std::string& functor_prefix, const std::string& atom_prefix) const {
  if (is_leaf_) {
    // Non-functor atom term
    return ToPrologAtom(quotes_atoms, atom_prefix);
  } else {
    // Compound term
    assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
    assert(children_.front().IsLeaf() && "Compound term must start with functor.");
    std::ostringstream o;
    // Functor
    o << children_.front().ToPrologFunctor(quotes_atoms, functor_prefix);
    o << '(';
    // Arguments
    for (auto i = children_.begin() + 1; i != children_.end(); ++i) {
      if (i != children_.begin() + 1) {
        o << ", ";
      }
      o << i->ToPrologTerm(quotes_atoms, functor_prefix, atom_prefix);
    }
    o << ')';
    return o.str();
  }
}

std::string TreeNode::ToPrologClause(const bool quotes_atoms, const std::string& functor_prefix, const std::string& atom_prefix) const {
  if (is_leaf_) {
    // Fact clause of atom term
    return ToPrologTerm(quotes_atoms, functor_prefix, atom_prefix) + '.';
  } else {
    assert(!children_.empty() && "Empty clause is not allowed.");
    assert(children_.front().IsLeaf() && "Compound term must start with functor.");
    if (children_.front().GetValue() == "<=") {
      assert(children_.size() >= 2 && "Rule clause must have head.");
      // Rule clause
      std::ostringstream o;
      // Head
      o << children_.at(1).ToPrologTerm(quotes_atoms, functor_prefix, atom_prefix);
      if (children_.size() >= 3) {
        // Body
        o << " :- ";
        for (auto i = children_.begin() + 2; i != children_.end(); ++i) {
          if (i != children_.begin() + 2) {
            o << ", ";
          }
          o << i->ToPrologTerm(quotes_atoms, functor_prefix, atom_prefix);
        }
      }
      o << '.';
      return o.str();
    } else {
      // Fact clause of compound term
      return ToPrologTerm(quotes_atoms, functor_prefix, atom_prefix) + '.';
    }
  }
}

std::unordered_set<std::string> TreeNode::CollectAtoms() const {
  if (is_leaf_) {
    if (value_ == "<=" || value_.front() == '?') {
      // Not atom
      return std::unordered_set<std::string>();
    } else {
      // Atom
      return std::unordered_set<std::string>({value_});
    }
  } else {
    // Compound term
    assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
    assert(children_.front().IsLeaf() && "Compound term must start with functor.");
    std::unordered_set<std::string> values;
    for (const auto& child : children_) {
      const auto& child_atoms = child.CollectAtoms();
      values.insert(child_atoms.begin(), child_atoms.end());
    }
    return values;
  }
}

std::unordered_set<std::string> TreeNode::CollectNonFunctorAtoms() const {
  if (is_leaf_) {
    if (value_ == "<=" || value_.front() == '?') {
      // Not atom
      return std::unordered_set<std::string>();
    } else {
      // Non-functor atom
      return std::unordered_set<std::string>({value_});
    }
  } else {
    // Compound term
    assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
    assert(children_.front().IsLeaf() && "Compound term must start with functor.");
    std::unordered_set<std::string> values;
    // Ignore functor and search non-functor arguments
    for (auto i = children_.begin() + 1; i != children_.end(); ++i) {
      const auto& child_atoms = i->CollectNonFunctorAtoms();
      values.insert(child_atoms.begin(), child_atoms.end());
    }
    return values;
  }
}

void TreeNode::CollectFunctorAtoms(
    std::unordered_map<std::string, int>& output) const {
  if (is_leaf_) {
    return;
  } else {
    // Compound term
    assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
    assert(children_.front().IsLeaf() && "Compound term must start with functor.");
    // Functor
    if (children_.front().GetValue() != "<=") {
      output.emplace(children_.front().GetValue(), children_.size() - 1);
    }
    // Search compound term arguments
    for (auto i = children_.begin() + 1; i != children_.end(); ++i) {
      i->CollectFunctorAtoms(output);
    }
  }
}

std::unordered_map<std::string, int> TreeNode::CollectFunctorAtoms() const {
  std::unordered_map<std::string, int> tmp;
  CollectFunctorAtoms(tmp);
  return tmp;
}

std::unordered_map<std::string, std::unordered_set<ArgPos>> TreeNode::CollectVariableArgs() const {
  assert(!is_leaf_);
  // Compound term
  assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
  assert(children_.front().IsLeaf() && "Compound term must start with functor.");
  std::unordered_map<std::string, std::unordered_set<ArgPos>> values;
  const auto functor = children_.front().GetValue();
  // Ignore functor and search non-functor arguments
  for (auto i = children_.begin() + 1; i != children_.end(); ++i) {
    if (i->IsLeaf()) {
      if (i->IsVariable()) {
        const auto variable_name = i->GetValue();
        const auto pos = std::distance(children_.begin(), i);
        if (values.count(i->GetValue())) {
          values.at(variable_name).emplace(functor, pos);
        } else {
          values.emplace(variable_name, std::unordered_set<ArgPos>({ ArgPos(functor, pos) }));
        }
      }
    } else {
      const auto tmp = i->CollectVariableArgs();
      for (const auto t : tmp) {
        if (values.count(t.first)) {
          values.at(t.first).insert(t.second.begin(), t.second.end());
        } else {
          values.insert(t);
        }
      }
    }
  }
  return values;
}

using VariableArgPosMap = const std::unordered_map<std::string, std::unordered_set<ArgPos>>;

std::unordered_set<ArgPosPair> VariableArgPosToArgPosPairs(const VariableArgPosMap& head_variable_args, const VariableArgPosMap& body_variable_args) {
  std::unordered_set<ArgPosPair> result;
  for (const auto head_variable_and_positions : head_variable_args) {
    const auto& variable_name = head_variable_and_positions.first;
    const auto& head_positions = head_variable_and_positions.second;
    if (!body_variable_args.count(variable_name)) {
      continue;
    }
    const auto& body_positions = body_variable_args.at(variable_name);
    for (const auto& head_pos : head_positions) {
      for (const auto& body_pos : body_positions) {
        // Head first, body second
        result.emplace(head_pos, body_pos);
      }
    }
  }
  return result;
}

std::unordered_set<ArgPosPair> VariableArgPosToArgPosPairs(const VariableArgPosMap& variable_args) {
  std::unordered_set<ArgPosPair> result;
  for (const auto variable_and_positions : variable_args) {
    const auto& positions = variable_and_positions.second;
    assert(!positions.empty());
    if  (positions.size() == 1) {
      continue;
    }
    auto sorted_positions = std::vector<ArgPos>(positions.begin(), positions.end());
    std::sort(sorted_positions.begin(), sorted_positions.end(), [](const ArgPos& p, const ArgPos& q){
      if (p.first == q.first) {
        return p.second < q.second;
      } else {
        return p.first < q.first;
      }
    });
    for (auto i = sorted_positions.begin(); i != sorted_positions.end() - 1; ++i) {
      for (auto j = i + 1; j != sorted_positions.end(); ++j) {
        result.emplace(*i, *j);
      }
    }
  }
  return result;
}

std::unordered_set<ArgPosPair> TreeNode::CollectSameDomainArgsBetweenHeadAndBody() const {
  assert(!is_leaf_);
  assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
  assert(children_.front().IsLeaf() && "Compound term must start with functor.");
  assert(children_.front().GetValue() == "<=");
  if (children_.size() == 2) {
    // Only head
    return std::unordered_set<ArgPosPair>();
  }
  if (children_.at(1).IsLeaf()) {
    // Head is leaf
    return std::unordered_set<ArgPosPair>();
  }
  // Head
  const auto head_variable_args = children_.at(1).CollectVariableArgs();
  // Body
  std::unordered_map<std::string, std::unordered_set<ArgPos>> body_variable_args;
  for (auto i = children_.begin() + 2; i != children_.end(); ++i) {
    if (!i->IsLeaf()) {
      const auto tmp = i->CollectVariableArgs();
      for (const auto t : tmp) {
        if (body_variable_args.count(t.first)) {
          body_variable_args.at(t.first).insert(t.second.begin(), t.second.end());
        } else {
          body_variable_args.insert(t);
        }
      }
    }
  }
  // Intersection of head and body
  return VariableArgPosToArgPosPairs(head_variable_args, body_variable_args);
}
std::unordered_set<ArgPosPair> TreeNode::CollectSameDomainArgsInBody() const {
  assert(!is_leaf_);
  assert(children_.size() >= 2  && "Compound term must have a functor and one or more arguments.");
  assert(children_.front().IsLeaf() && "Compound term must start with functor.");
  assert(children_.front().GetValue() == "<=");
  std::unordered_map<std::string, std::unordered_set<ArgPos>> variable_args;
  for (auto i = children_.begin() + 2; i != children_.end(); ++i) {
    if (!i->IsLeaf()) {
      const auto tmp = i->CollectVariableArgs();
      for (const auto t : tmp) {
        if (variable_args.count(t.first)) {
          variable_args.at(t.first).insert(t.second.begin(), t.second.end());
        } else {
          variable_args.insert(t);
        }
      }
    }
  }
  return VariableArgPosToArgPosPairs(variable_args);
}

TreeNode TreeNode::ReplaceAtoms(const std::string& before, const std::string& after) const {
  if (is_leaf_) {
    if (value_ == before) {
      return TreeNode(after);
    } else {
      return TreeNode(value_);
    }
  } else {
    std::vector<TreeNode> new_children;
    for (const auto& child : children_) {
      new_children.push_back(child.ReplaceAtoms(before, after));
    }
    return TreeNode(new_children);
  }
}

bool TreeNode::CollectDynamicRelations(
    std::unordered_set<std::string>& local_relations) const {
  if (is_leaf_) {
    return false;
  }
  assert(children_.size() >= 2);
  assert(children_.front().IsLeaf());
  if (children_.front().GetValue() != "<=") {
    return false;
  }
  if (children_.size() == 2) {
    // No body
    return false;
  }
  const auto& functor = children_.at(1).GetFunctor();
  if (local_relations.count(functor)) {
    // Already proved to be local
    return false;
  }
  for (auto i = children_.begin() + 2; i != children_.end(); ++i) {
    if (i->ContainsFunctors(local_relations)) {
      local_relations.insert(functor);
      return true;
    }
  }
  return false;
}

const std::string& TreeNode::GetFunctor() const {
  if (is_leaf_) {
    return value_;
  } else {
    assert(!children_.empty());
    assert(children_.front().IsLeaf());
    return children_.front().GetValue();
  }
}

bool TreeNode::ContainsFunctors(const std::unordered_set<std::string>& functors) const {
  if (is_leaf_) {
    return functors.count(value_);
  } else {
    for (const auto& child : children_) {
      if (child.ContainsFunctors(functors)) {
        return true;
      }
    }
    return false;
  }
}

bool TreeNode::operator==(const TreeNode& another) const {
  if (is_leaf_) {
    if (another.IsLeaf()) {
      return value_ == another.GetValue();
    } else {
      return false;
    }
  } else {
    if (!another.IsLeaf()) {
      return children_ == another.GetChildren();
    } else {
      return false;
    }
  }
}

void TreeNode::CollectNegatedFunctors(
    std::unordered_map<std::string, int>& output) const {
  if (is_leaf_) {
    return;
  } else {
    if (GetFunctor() == "not") {
      assert(children_.size() == 2);
      children_.back().CollectFunctorAtoms(output);
    } else {
      for (const auto& child : children_) {
        child.CollectNegatedFunctors(output);
      }
    }
  }
}

std::string RemoveComments(const std::string& sexpr) {
  boost::regex comment(";.*?$");
  return boost::regex_replace(sexpr, comment, std::string());
}

// Argument iterator must point to the token next to "("
// When this function returns, iterator will point to corresponding ")"
TreeNode ParseTillRightParen(Tokenizer::iterator& i, const bool flatten_tuple_with_one_child) {
  std::vector<TreeNode> children;
  for (; *i != ")"; ++i) {
    if (*i == "(") {
      // Skip left paren and parse inside parens
      ++i;
      children.push_back(ParseTillRightParen(i, flatten_tuple_with_one_child));
    } else {
      children.push_back(TreeNode(*i));
    }
  }
  if (flatten_tuple_with_one_child && children.size() == 1) {
    return TreeNode(children.front());
  }
  // When this function returns, the iterator is always pointing to ")"
  return TreeNode(children);
}

std::vector<TreeNode> Parse(const std::string& sexpr, bool flatten_tuple_with_one_child) {
  Separator sep(" \n\t\r", "()");
  const auto sexpr_no_comments = RemoveComments(sexpr);
  Tokenizer tokens(sexpr_no_comments, sep);
  std::vector<TreeNode> results;
  for (auto i = tokens.begin(); i != tokens.end(); ++i) {
    if (*i == "(") {
      // Skip left paren and parse inside parens
      ++i;
      results.push_back(ParseTillRightParen(i, flatten_tuple_with_one_child));
    } else {
      results.push_back(TreeNode(*i));
    }
  }
  return results;
}

std::vector<TreeNode> ParseKIF(const std::string& kif) {
  return Parse(kif, true);
}

std::unordered_set<std::string> CollectNonGroundRelations(const std::vector<TreeNode>& nodes) {
  std::unordered_set<std::string> non_grounds;
  for (const auto& node : nodes) {
    if (node.IsLeaf()) {
      continue;
    }
    if (node.GetChildren().front().GetValue() == "<=") {
      non_grounds.insert(node.GetChildren().at(1).GetFunctor());
    }
  }
  return non_grounds;
}

std::string GenerateUserDefinedFunctorClauses(
    const std::unordered_map<std::string, int>& functors,
    const bool quotes_atoms,
    const std::string& functor_prefix) {
  std::ostringstream o;
  for (const auto& functor_arity_pair : functors) {
    if (kReservedRelations.count(functor_arity_pair.first)) {
      continue;
    }
    const auto functor_atom =
        ConvertToPrologFunctor(
            functor_arity_pair.first,
            quotes_atoms,
            functor_prefix);
    o << "user_defined_functor(" << functor_atom << ", " << functor_arity_pair.second << ")." << std::endl;
  }
  return o.str();
}

std::string GenerateTableClauses(
    const std::vector<TreeNode>& nodes,
    const bool quotes_atoms,
    const std::string& functor_prefix) {
  std::ostringstream o;
  const auto static_relations = CollectStaticRelations(nodes);
  for (const auto& functor_arity_pair : static_relations) {
    const auto functor_atom =
        ConvertToPrologFunctor(
            functor_arity_pair.first,
            quotes_atoms,
            functor_prefix);
    o << boost::format(":- table %1%/%2%.") %
        functor_atom %
        functor_arity_pair.second << std::endl;
  }
  return o.str();
}

std::string GeneratePrologHelperClauses(
    const std::vector<TreeNode>& nodes,
    const bool quotes_atoms,
    const std::string& functor_prefix,
    const std::string& atom_prefix) {
  std::ostringstream o;
  const auto functors = CollectFunctorAtoms(nodes);
  // User defined functors
  o << GenerateUserDefinedFunctorClauses(functors, quotes_atoms, functor_prefix);
  // Same domain args
  std::unordered_set<ArgPosPair> same_domain_args_pairs_in_body;
  std::unordered_set<ArgPosPair> same_domain_args_pairs_between_head_and_body;
  for (const auto& node : nodes) {
    if (!node.IsLeaf() && node.GetChildren().front().GetValue() == "<=") {
      const auto in_body = node.CollectSameDomainArgsInBody();
      same_domain_args_pairs_in_body.insert(in_body.begin(), in_body.end());
      const auto between_head_and_body = node.CollectSameDomainArgsBetweenHeadAndBody();
      same_domain_args_pairs_between_head_and_body.insert(between_head_and_body.begin(), between_head_and_body.end());
    }
  }
  for (const auto& same_domain_args_pair : same_domain_args_pairs_in_body) {
    if (same_domain_args_pairs_between_head_and_body.count(same_domain_args_pair)) {
      continue;
    }
    const auto functor_atom1 = ConvertToPrologFunctor(same_domain_args_pair.first.first, quotes_atoms, functor_prefix);
    const auto pos1 = same_domain_args_pair.first.second;
    const auto functor_atom2 = ConvertToPrologFunctor(same_domain_args_pair.second.first, quotes_atoms, functor_prefix);
    const auto pos2 = same_domain_args_pair.second.second;
    o << "connected_args(" << functor_atom1 << ", " << pos1 << ", " << functor_atom2 << ", " << pos2 << ")." << std::endl;
  }
  for (const auto& same_domain_args_pair : same_domain_args_pairs_between_head_and_body) {
    const auto functor_atom1 = ConvertToPrologFunctor(same_domain_args_pair.first.first, quotes_atoms, functor_prefix);
    const auto pos1 = same_domain_args_pair.first.second;
    const auto functor_atom2 = ConvertToPrologFunctor(same_domain_args_pair.second.first, quotes_atoms, functor_prefix);
    const auto pos2 = same_domain_args_pair.second.second;
    o << "equivalent_args(" << functor_atom1 << ", " << pos1 << ", " << functor_atom2 << ", " << pos2 << ")." << std::endl;
  }
  // Non-ground relations
  const auto non_grounds = CollectNonGroundRelations(nodes);
  for (const auto& non_ground : non_grounds) {
    if (kReservedRelations.count(non_ground)) {
      continue;
    }
    auto atom = non_ground;
    if (functors.count(non_ground)) {
      atom = functor_prefix + atom;
    } else {
      atom = atom_prefix + atom;
    }
    if (quotes_atoms) {
      atom = '\'' + atom + '\'';
    }
    o << "non_ground(" << atom << ")." << std::endl;
  }
  return o.str();
}

std::string ToProlog(
    const std::vector<TreeNode>& nodes,
    const bool quotes_atoms,
    const std::string& functor_prefix,
    const std::string& atom_prefix,
    const bool adds_helper_clauses,
    const bool enables_tabling) {
  std::ostringstream o;
  if (enables_tabling) {
    o << GenerateTableClauses(nodes, quotes_atoms, functor_prefix);
  }
  for (const auto& node : nodes) {
    o << node.ToPrologClause(quotes_atoms, functor_prefix, atom_prefix) << std::endl;
  }
  if (adds_helper_clauses) {
    o << GeneratePrologHelperClauses(nodes, quotes_atoms, functor_prefix, atom_prefix) << std::endl;
  }
  return o.str();
}

std::unordered_set<std::string> CollectAtoms(const std::vector<TreeNode>& nodes) {
  std::unordered_set<std::string> values;
  for (const auto& node : nodes) {
    const auto& node_values = node.CollectAtoms();
    values.insert(node_values.begin(), node_values.end());
  }
  return values;
}

std::unordered_set<std::string> CollectNonFunctorAtoms(const std::vector<TreeNode>& nodes) {
  std::unordered_set<std::string> values;
  for (const auto& node : nodes) {
    const auto& node_values = node.CollectNonFunctorAtoms();
    values.insert(node_values.begin(), node_values.end());
  }
  return values;
}

std::unordered_map<std::string, int> CollectFunctorAtoms(
    const std::vector<TreeNode>& nodes) {
  std::unordered_map<std::string, int> values;
  for (const auto& node : nodes) {
    node.CollectFunctorAtoms(values);
  }
  return values;
}

std::vector<TreeNode> ReplaceAtoms(const std::vector<TreeNode>& nodes, const std::string& before, const std::string& after) {
  std::vector<TreeNode> new_nodes;
  for (const auto& node : nodes) {
    new_nodes.push_back(node.ReplaceAtoms(before, after));
  }
  return new_nodes;
}

std::unordered_set<std::string> CollectClauseHeadRelations(
    const std::vector<TreeNode>& nodes) {
  std::unordered_set<std::string> head_relations;
  for (const auto& node : nodes) {
    if (node.GetFunctor() == "<=") {
      head_relations.emplace(node.GetChildren()[1].GetFunctor());
    } else {
      head_relations.emplace(node.GetFunctor());
    }
  }
  return head_relations;
}

std::unordered_map<std::string, int> CollectNegatedFunctors(
    const std::vector<TreeNode>& nodes) {
  std::unordered_map<std::string, int> negated_relations;
  for (const auto& node : nodes) {
    node.CollectNegatedFunctors(negated_relations);
  }
  return negated_relations;
}

std::unordered_set<std::string> CollectDynamicRelations(
    const std::vector<TreeNode>& nodes) {
  auto dynamic_relations = kReservedDynamicRelations; // copy
  while (true) {
    auto found = false;
    for (const auto& node : nodes) {
      if (node.CollectDynamicRelations(dynamic_relations)) {
        // If a new dynamic relation is found, restart the search again
        // so that we can find relations depending on the newly found relation.
        found = true;
      }
    }
    if (!found) {
      break;
    }
  }
  return dynamic_relations;
}

std::unordered_map<std::string, int> CollectStaticRelations(
    const std::vector<TreeNode>& nodes) {
  const auto dynamic_relations = CollectDynamicRelations(nodes);
  const auto head_relations = CollectClauseHeadRelations(nodes);
  const auto functors = CollectFunctorAtoms(nodes);
  const auto negated_functors = CollectNegatedFunctors(nodes);
  std::unordered_map<std::string, int> global_relations;
  for (const auto& rel : head_relations) {
    if (!kReservedRelations.count(rel) &&
        !dynamic_relations.count(rel) &&
        !negated_functors.count(rel)) {
//      if (functor.first == "cell" || functor.first == "control" || functor.first == "mark") {
//        continue;
//      }
//      if (functor.first == "index" || functor.first == "cellHolds" || functor.first == "control" || functor.first == "move") {
//        continue;
//      }
//      auto allowed = std::unordered_set<std::string>{{
////        "countplus",
////        "horizontal",
////        "diagonalup",
////        "scoremap",
////        "pp",
////        "diagonaldown",
////        "kingmove",
////        "distinctcell",
////        "cell",
////        "knightmove",
////        "owns",
////        "vertical",
////        "file",
////        "rank",
//        "type"
//      }};
//      if (!allowed.count(rel)) {
//        continue;
//      }
      global_relations.emplace(rel, functors.at(rel));
    }
  }
  return global_relations;
}


}
