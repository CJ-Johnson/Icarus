#include "Parser.h"
#include "AST/Expression.h"

Parser::Parser(const char* filename) : lexer_(filename) {
  init_rules();
}

void Parser::parse() {
  while (lexer_) {
    // Reduce if you can
    while (reduce()) {
//      for (const auto& node_ptr : stack_) {
//        std::cout << *node_ptr << std::endl;
//      }
//      std::cout << std::endl;
    }

    // Otherwise shift
    shift();
//    for (const auto& node_ptr : stack_) {
//      std::cout << *node_ptr << std::endl;
//    }
//    std::cout << std::endl;
  }

  for (const auto& node_ptr : stack_) {
    std::cout << *node_ptr << std::endl;
  }
  std::cout << std::endl;
}

bool Parser::reduce() {
  const Rule* matched_rule_ptr = nullptr;

  for (const Rule& rule : rules_) {
    // If we've already found a rule, ignore rules of lower precedence
    if (matched_rule_ptr != nullptr &&
        matched_rule_ptr->size() > rule.size()) {
      continue;
    }

    if (rule.match(stack_)) {
#ifdef DEBUG
      if (matched_rule_ptr != nullptr &&
          rule.size() == matched_rule_ptr->size()) {
        throw "Two rules matched with the same size";
      }
#endif

      matched_rule_ptr = &rule;
    }
  }

  if (matched_rule_ptr == nullptr) return false;

  matched_rule_ptr->apply(stack_);

  return true;
}

void Parser::init_rules() {
  using AST::Node;

  rules_.push_back(Rule(Node::expression, {
        Node::identifier
        }, AST::Terminal::build_identifier));

  rules_.push_back(Rule(Node::expression, {
        Node::integer
        }, AST::Terminal::build_integer));

  rules_.push_back(Rule(Node::expression, {
        Node::real
        }, AST::Terminal::build_real));


  rules_.push_back(Rule(Node::paren_expression, {
        Node::left_paren, Node::expression, Node::right_paren
        }, AST::Expression::parenthesize));

  rules_.push_back(Rule(Node::expression, {
        Node::expression, Node::operat, Node::expression
        }, AST::Binop::build));

  rules_.push_back(Rule(Node::expression, {
        Node::paren_expression, Node::operat, Node::expression
        }, AST::Binop::build));

  rules_.push_back(Rule(Node::expression, {
        Node::expression, Node::operat, Node::paren_expression
        }, AST::Binop::build));

  rules_.push_back(Rule(Node::expression, {
        Node::paren_expression, Node::operat, Node::paren_expression
        }, AST::Binop::build));

  // TODO(andy) write explicit operator for this
  rules_.push_back(Rule(Node::expression, {
        Node::expression, Node::left_paren, Node::expression, Node::right_paren
        }, AST::Binop::build));
}
