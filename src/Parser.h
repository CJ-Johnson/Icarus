#ifndef ICARUS_PARSER_H
#define ICARUS_PARSER_H

#include <memory>
#include <iostream>

#include "Lexer.h"
#include "Rule.h"
#include "AST/Node.h"
#include "typedefs.h"

class Parser {
  public:

    Parser(const char* filename);

    void parse();

  private:
    void shift();
    bool reduce();

    void init_rules();

    std::vector<Rule> rules_;
    std::vector<NPtr> stack_;
    Lexer lexer_;
};

inline void Parser::shift() {
  NPtr next_node_ptr(new AST::Node);
  lexer_ >> *next_node_ptr;

  // Never shift comments onto the stack
  if (next_node_ptr->node_type() == AST::Node::comment)
    return;

  stack_.push_back(std::move(next_node_ptr));
}

#endif  // ICARUS_PARSER_H
