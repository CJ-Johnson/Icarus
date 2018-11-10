#ifndef ICARUS_AST_STATEMENTS_H
#define ICARUS_AST_STATEMENTS_H

#include "base/container/vector.h"

#include "node.h"

namespace AST {
struct Statements : public Node {
  Statements() {}
  ~Statements() override {}
  Statements(Statements &&) noexcept = default;
  Statements &operator=(Statements &&) noexcept = default;

  std::string to_string(size_t n) const override;
  void assign_scope(Scope *scope) override;
  type::Type const *VerifyType(Context *) override;
  void Validate(Context *) override;
  void ExtractJumps(JumpExprs *) const override;
  
  base::vector<IR::Val> EmitIR(Context *) override;

  inline size_t size() const { return content_.size(); }

  static Statements Merge(base::vector<Statements> &&stmts_vec) {
    size_t num_stmts = 0;
    for (const auto &stmts : stmts_vec) { num_stmts += stmts.size(); }

    Statements result;
    result.content_.reserve(num_stmts);

    for (auto &&stmts : stmts_vec) {
      for (auto &&stmt : stmts.content_) {
        result.content_.push_back(std::move(stmt));
      }
    }

    return result;
  }

  base::vector<std::unique_ptr<Node>> content_;
};

}  // namespace AST
#endif  // ICARUS_AST_STATEMENTS_H
