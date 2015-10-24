#ifndef ICARUS_AST_H
#define ICARUS_AST_H

#include <string>
#include <iostream>
#include <map>
#include <set>
#include <vector>

// TODO Figure out what you need from this.
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

#include "Language.h"
#include "Type.h"
#include "typedefs.h"
#include "ScopeDB.h"

namespace AST {
  using ::ScopeDB::Scope;
  extern size_t function_counter;

  class Declaration;
  class Statements;

  class Node {
    public:
      static inline Node eof_node() { return Node(Language::eof, ""); }
      static inline Node newline_node() { return Node(Language::newline, ""); }
      static inline Node string_literal_node(const std::string& str_lit) { 
        return Node(Language::string_literal, str_lit);
      }

      Language::NodeType node_type() const { return type_; }
      void set_node_type(Language::NodeType t) { type_ = t; }

      std::string token() const { return token_; }
      void set_token(const std::string& token_string) {
        token_ = token_string;
      }

      virtual std::string to_string(size_t n) const;
      virtual void join_identifiers(Scope* scope) {}
      virtual void verify_types() {}

      virtual llvm::Value* generate_code(Scope* scope,
          llvm::IRBuilder<>& builder) { return nullptr; }

      virtual bool is_identifier() const {
        return type_ == Language::identifier;
      }

      bool is_return() const {
        return node_type() == Language::return_expression;
      }
      virtual bool is_binop() const { return false; }
      virtual bool is_chain_op() const { return false; }
      virtual bool is_declaration() const { return false; }

      Node(Language::NodeType type = Language::unknown,
          const std::string& token = "") : type_(type), token_(token) {}

      virtual ~Node(){}


      inline friend std::ostream& operator<<(
          std::ostream& os, const Node& node) {

        return os << node.to_string(0);
      }

    protected:
      Language::NodeType type_;
      std::string token_;
  };


  class Expression : public Node {
    friend void ::ScopeDB::Scope::determine_declared_types();
    friend class KVPairList;
    friend class Unop;
    friend class Binop;
    friend class ChainOp;
    friend class Declaration;
    friend class Assignment;
    friend void ScopeDB::determine_declared_types();

    public:
    static NPtr parenthesize(NPtrVec&& nodes);

    size_t precedence() const { return precedence_; }

    virtual std::string to_string(size_t n) const = 0;
    virtual void join_identifiers(Scope* scope) = 0;
    virtual void needed_for(IdPtr id_ptr) const = 0;
    virtual void verify_types() = 0;
    virtual void verify_type_is(Type t);

    virtual Type interpret_as_type() const = 0;
    virtual Type type() const { return expr_type_; }


    virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder) = 0;

    virtual ~Expression(){}

    protected:
    Expression() {}

    size_t precedence_;
    Type expr_type_;
  };

  inline NPtr Expression::parenthesize(NPtrVec&& nodes) {
    auto expr_ptr = std::static_pointer_cast<Expression>(nodes[1]);
    expr_ptr->precedence_ = Language::op_prec.at("MAX");
    return expr_ptr;
  }


  inline void Expression::verify_type_is(Type t) {
    verify_types();

    if (expr_type_ != t) {
      // TODO: give some context for this error message. Why must this be the
      // type?  So far the only instance where this is called is for case
      // statements,
      std::cerr
        << "Type of `____` must be " << t.to_string() << ", but "
        << expr_type_.to_string() << " found instead." << std::endl;
      expr_type_ = t;
    }
  }

  // TODO: This only represents a left unary operator for now
  class Unop : public Expression {
    friend class Statements;

    public:
    static NPtr build(NPtrVec&& nodes);

    virtual std::string to_string(size_t n) const;
    virtual void join_identifiers(Scope* scope);
    virtual void needed_for(IdPtr id_ptr) const;
    virtual void verify_types();

    virtual Type interpret_as_type() const;


    virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

    private:
    EPtr expr_;
  };


  inline NPtr Unop::build(NPtrVec&& nodes) {
    auto unop_ptr = new Unop;
    unop_ptr->expr_ = std::static_pointer_cast<Expression>(nodes[1]);

    unop_ptr->type_ = Language::expression;
    if (nodes[0]->node_type() == Language::reserved_return) {
      unop_ptr->token_ = "return";
    } else {
      unop_ptr->token_ = nodes[0]->token();
    }

    unop_ptr->precedence_ = Language::op_prec.at(unop_ptr->token());

    return NPtr(unop_ptr);
  }



  class Binop : public Expression {
    friend class KVPairList;
    friend class FunctionLiteral;

    public:
    static NPtr build_operator(NPtrVec&& nodes, std::string op_symbol);

    static NPtr build(NPtrVec&& nodes);
    static NPtr build_paren_operator(NPtrVec&& nodes);
    static NPtr build_bracket_operator(NPtrVec&& nodes);

    virtual std::string to_string(size_t n) const;
    virtual void join_identifiers(Scope* scope);
    virtual void needed_for(IdPtr id_ptr) const;
    virtual void verify_types();

    virtual Type interpret_as_type() const;
    virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

    virtual bool is_binop() const { return true; }


    virtual ~Binop(){}

    protected:
    Binop() {}
    EPtr lhs_;
    EPtr rhs_;
  };

  inline NPtr Binop::build_paren_operator(NPtrVec&& nodes) {
    return Binop::build_operator(std::forward<NPtrVec>(nodes), "()");
  }

  inline NPtr Binop::build_bracket_operator(NPtrVec&& nodes) {
    return Binop::build_operator(std::forward<NPtrVec>(nodes), "[]");
  }

  inline NPtr Binop::build(NPtrVec&& nodes) {
    return Binop::build_operator(
        std::forward<NPtrVec>(nodes),
        nodes[1]->token());
  }

  inline NPtr Binop::build_operator(NPtrVec&& nodes, std::string op_symbol) {
    auto binop_ptr = new Binop;
    binop_ptr->lhs_ =
      std::static_pointer_cast<Expression>(nodes[0]);

    binop_ptr->rhs_ =
      std::static_pointer_cast<Expression>(nodes[2]);

    binop_ptr->token_ = op_symbol;
    binop_ptr->type_ = Language::generic_operator;

    binop_ptr->precedence_ = Language::op_prec.at(op_symbol);

    return NPtr(binop_ptr);
  }



  class ChainOp : public Expression {
    public:
      static NPtr build(NPtrVec&& nodes);

      virtual std::string to_string(size_t n) const;
      virtual void join_identifiers(Scope* scope);
      virtual void needed_for(IdPtr id_ptr) const;
      virtual void verify_types();

      virtual Type interpret_as_type() const;

      virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

      virtual bool is_chain_op() const { return true; }

    private:
      std::vector<NPtr> ops_;
      std::vector<EPtr> exprs_;
  };

  inline NPtr ChainOp::build(NPtrVec&& nodes) {
    std::shared_ptr<ChainOp> chain_ptr(nullptr);

    // Add to a chain so long as the precedence levels match. The only thing at
    // that precedence level should be the operators which can be chained.
    bool use_old_chain_op = nodes[0]->is_chain_op();
    if (use_old_chain_op) {
      ChainOp* lhs_ptr = static_cast<ChainOp*>(nodes[0].get());

      if (lhs_ptr->precedence() != Language::op_prec.at(nodes[1]->token())) {
        use_old_chain_op = false;
      }
    }

    if (use_old_chain_op) {
      chain_ptr = std::static_pointer_cast<ChainOp>(nodes[0]);

    } else {
      chain_ptr = std::shared_ptr<ChainOp>(new ChainOp);
      chain_ptr->exprs_.push_back(std::static_pointer_cast<Expression>(nodes[0]));
      chain_ptr->precedence_ = Language::op_prec.at(nodes[1]->token());
    }

    chain_ptr->ops_.push_back(nodes[1]);

    chain_ptr->exprs_.push_back(
        std::static_pointer_cast<Expression>(nodes[2]));

    return std::static_pointer_cast<Node>(chain_ptr);
  }




  class Terminal : public Expression {
    friend class KVPairList;

    public:
    static NPtr build(NPtrVec&& nodes, Type t);
    static NPtr build_type_literal(NPtrVec&& nodes);
    static NPtr build_string_literal(NPtrVec&& nodes);
    static NPtr build_integer_literal(NPtrVec&& nodes);
    static NPtr build_real_literal(NPtrVec&& nodes);
    static NPtr build_character_literal(NPtrVec&& nodes);

    virtual std::string to_string(size_t n) const;
    virtual void join_identifiers(Scope* scope) {}
    virtual void needed_for(IdPtr id_ptr) const {};
    virtual void verify_types();

    virtual Type interpret_as_type() const;
    virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

    protected:
    Terminal() {}
  };

  inline NPtr Terminal::build(NPtrVec&& nodes, Type t) {
    auto term_ptr = new Terminal;
    term_ptr->expr_type_ = t;
    term_ptr->token_ = nodes[0]->token();
    term_ptr->precedence_ = Language::op_prec.at("MAX");

    return NPtr(term_ptr);
  }

  inline NPtr Terminal::build_type_literal(NPtrVec&& nodes) {
    return build(std::forward<NPtrVec>(nodes), Type::Type_);
  }

  inline NPtr Terminal::build_string_literal(NPtrVec&& nodes) {
    return build(std::forward<NPtrVec>(nodes), Type::String);
  }

  inline NPtr Terminal::build_integer_literal(NPtrVec&& nodes) {
    return build(std::forward<NPtrVec>(nodes), Type::Int);
  }

  inline NPtr Terminal::build_real_literal(NPtrVec&& nodes) {
    return build(std::forward<NPtrVec>(nodes), Type::Real);
  }

  inline NPtr Terminal::build_character_literal(NPtrVec&& nodes) {
    return build(std::forward<NPtrVec>(nodes), Type::Char);
  }


  class Assignment : public Binop {
    public:
      static NPtr build(NPtrVec&& nodes);

      virtual std::string to_string(size_t n) const;
      virtual void verify_types();

      virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

      virtual ~Assignment(){}

    private:
      Assignment() {}
  };

  inline NPtr Assignment::build(NPtrVec&& nodes) {
    auto assign_ptr = new Assignment;
    assign_ptr->lhs_ = std::static_pointer_cast<Expression>(nodes[0]);
    assign_ptr->rhs_ = std::static_pointer_cast<Expression>(nodes[2]);

    assign_ptr->token_ = ":";
    assign_ptr->type_ = Language::assign_operator;

    assign_ptr->precedence_ = Language::op_prec.at("=");

    return NPtr(assign_ptr);
  }


  class Identifier : public Terminal {
    friend class Assignment;

    public:
      static NPtr build(NPtrVec&& nodes) {
        return NPtr(new Identifier(nodes[0]->token()));
      }

      virtual std::string to_string(size_t n) const;
      virtual void verify_types();

      virtual bool is_identifier() const { return true; }
      virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

      Identifier(const std::string& token_string) : alloca_(nullptr) {
        token_ = token_string;
        expr_type_ = Type::Unknown;
        precedence_ = Language::op_prec.at("MAX");
      }

      llvm::AllocaInst* alloca_;
  };


  class Declaration : public Expression {
    friend void ScopeDB::determine_declared_types();

    public:
    static NPtr build(NPtrVec&& nodes);

    std::string identifier_string() const { return id_->token(); }
    IdPtr declared_identifier() const { return id_; }
    EPtr declared_type() const { return decl_type_; }

    virtual std::string to_string(size_t n) const;
    virtual void join_identifiers(Scope* scope);
    virtual void needed_for(IdPtr id_ptr) const;
    virtual void verify_types();

    virtual Type interpret_as_type() const { return decl_type_->interpret_as_type(); }


    virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

    virtual bool is_declaration() const { return true; }

    virtual ~Declaration(){}


    Declaration() {}

    private:
    IdPtr id_;
    EPtr decl_type_;
  };

  inline NPtr Declaration::build(NPtrVec&& nodes) {
    auto decl_ptr = ScopeDB::make_declaration();

    decl_ptr->id_ = IdPtr(new Identifier(nodes[0]->token()));
    decl_ptr->decl_type_ = std::static_pointer_cast<Expression>(nodes[2]);

    decl_ptr->token_ = ":";
    decl_ptr->type_ = Language::decl_operator;

    decl_ptr->precedence_ = Language::op_prec.at(":");

    return std::static_pointer_cast<Node>(decl_ptr);
  }


  class KVPairList : public Node {
    public:
      static NPtr build_one(NPtrVec&& nodes);
      static NPtr build_more(NPtrVec&& nodes);
      static NPtr build_one_assignment_error(NPtrVec&& nodes);
      static NPtr build_more_assignment_error(NPtrVec&& nodes);


      virtual std::string to_string(size_t n) const;
      virtual void join_identifiers(Scope* scope);
      virtual Type verify_types_with_key(Type key_type);

    private:
      KVPairList() {}

      std::vector<std::pair<EPtr, EPtr>> kv_pairs_;
  };

  inline NPtr KVPairList::build_one(NPtrVec&& nodes) {
    auto pair_list = new KVPairList;
    EPtr key_ptr;

    if (nodes[0]->node_type() == Language::reserved_else) {
      key_ptr = EPtr(new Terminal);
      key_ptr->expr_type_ = Type::Bool;
      key_ptr->token_ = "else";
      key_ptr->precedence_ = Language::op_prec.at("MAX");

    } else {
      key_ptr = std::static_pointer_cast<Expression>(nodes[0]);
    }

    auto val_ptr = std::static_pointer_cast<Expression>(nodes[2]);

    pair_list->kv_pairs_.emplace_back(key_ptr, val_ptr);
    return NPtr(pair_list);
  }

  inline NPtr KVPairList::build_more(NPtrVec&& nodes) {
    auto pair_list = std::static_pointer_cast<KVPairList>(nodes[0]);
    EPtr key_ptr;

    if (nodes[1]->node_type() == Language::reserved_else) {
      key_ptr = EPtr(new Terminal);
      key_ptr->expr_type_ = Type::Bool;
      key_ptr->token_ = "else";
      key_ptr->precedence_ = Language::op_prec.at("MAX");

    } else {
      key_ptr = std::static_pointer_cast<Expression>(nodes[1]);
    }

    auto val_ptr = std::static_pointer_cast<Expression>(nodes[3]);

    pair_list->kv_pairs_.emplace_back(key_ptr, val_ptr);

    return pair_list;
  }

  inline NPtr KVPairList::build_one_assignment_error(NPtrVec&& nodes) {
    std::cerr << "You probably meant `==` instead of `=`" << std::endl;

    auto assignment_node = std::static_pointer_cast<Assignment>(nodes[0]);

    // TODO this is mostly the same code as Binop::build_operator
    auto binop_ptr = new Binop;
    binop_ptr->lhs_ = assignment_node->lhs_;
    binop_ptr->rhs_ = assignment_node->rhs_;

    binop_ptr->token_ = "==";
    binop_ptr->type_ = Language::generic_operator;
    binop_ptr->precedence_ = Language::op_prec.at("==");

    nodes[0] = NPtr(binop_ptr);


    return build_one(std::forward<NPtrVec>(nodes));
  }

  inline NPtr KVPairList::build_more_assignment_error(NPtrVec&& nodes) {
    std::cerr << "You probably meant `==` instead of `=`" << std::endl;

    auto assignment_node = std::static_pointer_cast<Assignment>(nodes[1]);

    // TODO this is mostly the same code as Binop::build_operator
    auto binop_ptr = new Binop;
    binop_ptr->lhs_ = assignment_node->lhs_;
    binop_ptr->rhs_ = assignment_node->rhs_;

    binop_ptr->token_ = "==";
    binop_ptr->type_ = Language::generic_operator;
    binop_ptr->precedence_ = Language::op_prec.at("==");

    nodes[0] = NPtr(binop_ptr);

    return build_more(std::forward<NPtrVec>(nodes));
  }



  class Case : public Expression {
    public:
      static NPtr build(NPtrVec&& nodes);

      virtual void join_identifiers(Scope* scope);
      virtual void needed_for(IdPtr id_ptr) const;

      virtual std::string to_string(size_t n) const;
      virtual void verify_types();

      virtual Type interpret_as_type() const;
      virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

    private:
      Case() {}
      std::shared_ptr<KVPairList> pairs_;
  };

  inline NPtr Case::build(NPtrVec&& nodes) {
    auto output = new Case;
    output->pairs_ = std::static_pointer_cast<KVPairList>(nodes[3]);
    return NPtr(output);
  }




  class Statements : public Node {

    public:
    static NPtr build_one(NPtrVec&& nodes);
    static NPtr build_more(NPtrVec&& nodes);

    virtual std::string to_string(size_t n) const;
    virtual void join_identifiers(Scope* scope);
    virtual void verify_types();

    void collect_return_types(std::set<Type>* return_exprs) const;
    virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

    inline size_t size() { return statements_.size(); }

    private:
    Statements() {}
    std::vector<NPtr> statements_;
  };

  inline NPtr Statements::build_one(NPtrVec&& nodes) {
    auto output = new Statements;
    output->statements_.push_back(std::move(nodes[0]));

    return NPtr(output);
  }

  inline NPtr Statements::build_more(NPtrVec&& nodes) {
    auto output = std::static_pointer_cast<Statements>(nodes[0]);
    output->statements_.push_back(std::move(nodes[1]));

    return NPtr(output);
  }


  class FunctionLiteral : public Expression {
    public:
      static NPtr build(NPtrVec&& nodes) {
        auto fn_lit = new FunctionLiteral;

        fn_lit->statements_ = std::static_pointer_cast<Statements>(nodes[2]);

        // TODO scopes inside these statements should point to fn_scope_.

        auto binop_ptr = std::static_pointer_cast<Binop>(nodes[0]);
        fn_lit->return_type_ = std::move(binop_ptr->rhs_);

        // TODO What if the fn_expression is more complicated, like a function
        // of the form (int -> int) -> int? I'm not sure how robust this is
        if (!binop_ptr->lhs_->is_declaration()) {
          // TODO Is this error message correct?
          std::cerr
            << "No input parameters named in function declaration"
            << std::endl;
        } else {
          // TODO This assumes a single declaration, not a comma-separated list
          auto decl_ptr = std::static_pointer_cast<Declaration>(binop_ptr->lhs_);

          fn_lit->inputs_.push_back(decl_ptr);
        }

        return NPtr(fn_lit);
      }

      virtual std::string to_string(size_t n) const;
      virtual void join_identifiers(Scope* scope);
      virtual void needed_for(IdPtr id_ptr) const;
      virtual void verify_types();

      virtual Type interpret_as_type() const;

      virtual llvm::Value* generate_code(Scope* scope,
        llvm::IRBuilder<>& builder);

      virtual ~FunctionLiteral() {}

    private:
      Scope* fn_scope_;
      EPtr return_type_;

      std::vector<DeclPtr> inputs_;
      llvm::BasicBlock* entry_block_;

      std::shared_ptr<Statements> statements_;

      FunctionLiteral() : fn_scope_(ScopeDB::Scope::build()), entry_block_(nullptr) {}
  };

  
  class While : public Node {
    public:
      static NPtr build(NPtrVec&& nodes) {
        auto while_stmt = new While;
        while_stmt->cond_ = std::static_pointer_cast<Expression>(nodes[1]);
        while_stmt->statements_ = std::static_pointer_cast<Statements>(nodes[3]);
        return NPtr(while_stmt);
      }

      virtual std::string to_string(size_t n) const;
      virtual void join_identifiers(Scope* scope);
      virtual void verify_types();

    private:
      EPtr cond_;
      std::shared_ptr<Statements> statements_;
      //Scope body_scope_;
  };


}  // namespace AST

#endif  // ICARUS_AST_NODE_H
