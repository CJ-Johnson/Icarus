#ifndef ICARUS_AST_REPEATED_UNOP_H
#define ICARUS_AST_REPEATED_UNOP_H

#include "ast/comma_list.h"
#include "ast/dispatch.h"
#include "ast/node.h"
#include "frontend/operators.h"

namespace AST {
struct RepeatedUnop : public Node {
  RepeatedUnop(TextSpan const &span);
  ~RepeatedUnop() override {}

  std::string to_string(size_t n) const override;
  void assign_scope(Scope *scope) override;
  type::Type const *VerifyType(Context *) override;
  void Validate(Context *) override;
  void ExtractJumps(JumpExprs *) const override;

  base::vector<IR::Val> EmitIR(Context *) override;

  Language::Operator op_;
  CommaList args_;
  base::vector<DispatchTable> dispatch_tables_;
};
}  // namespace AST
#endif  // ICARUS_AST_REPEATED_UNOP_H
