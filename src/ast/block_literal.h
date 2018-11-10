#ifndef ICARUS_AST_BLOCK_LITERAL_H
#define ICARUS_AST_BLOCK_LITERAL_H

#include "ast/expression.h"

namespace AST {
struct BlockLiteral : public Expression {
  BlockLiteral(bool required);
  ~BlockLiteral() override {}
  std::string to_string(size_t n) const override;
  void assign_scope(Scope *scope) override;
  type::Type const *VerifyType(Context *) override;
  void Validate(Context *) override;
  void ExtractJumps(JumpExprs *) const override;

  base::vector<IR::Val> EmitIR(Context *) override;
  base::vector<IR::Register> EmitLVal(Context *) override;

  std::vector<std::unique_ptr<Expression>> before_, after_;
  std::unique_ptr<Scope> body_scope_;
  bool required_;
};
}  // namespace AST

#endif  // ICARUS_AST_BLOCK_LITERAL_H
