#include "ast/array_literal.h"

#include "ast/verify_macros.h"
#include "context.h"
#include "error/log.h"
#include "ir/cmd.h"
#include "type/array.h"

namespace AST {
void ArrayLiteral::assign_scope(Scope *scope) {
  STAGE_CHECK(AssignScopeStage, AssignScopeStage);
  scope_ = scope;
  for (auto &el : elems_) { el->assign_scope(scope); }
}

std::string ArrayLiteral::to_string(size_t n) const {
  std::stringstream ss;
  ss << "[";
  auto iter = elems_.begin();
  ss << (*iter)->to_string(n);
  ++iter;
  while (iter != elems_.end()) {
    ss << ", " << (*iter)->to_string(n);
    ++iter;
  }
  ss << "]";
  return ss.str();
}

void ArrayLiteral::ClearIdDecls() {
  stage_range_ = StageRange{};
  for (auto &el : elems_) { el->ClearIdDecls(); }
}

void ArrayLiteral::VerifyType(Context *ctx) {
  VERIFY_STARTING_CHECK_EXPR;

  lvalue = Assign::Const;
  if (elems_.empty()) {
    type = type::EmptyArray;
    return;
  }

  for (auto &elem : elems_) {
    elem->VerifyType(ctx);
    HANDLE_CYCLIC_DEPENDENCIES;
    limit_to(elem);
    if (elem->lvalue != Assign::Const) { lvalue = Assign::RVal; }
  }

  const type::Type *joined = type::Err;
  for (auto &elem : elems_) {
    joined = type::Join(joined, elem->type);
    if (joined == nullptr) { break; }
  }

  if (joined == nullptr) {
    // type::Types couldn't be joined. Emit an error
    ctx->error_log_.InconsistentArrayType(span);
    type = type::Err;
    limit_to(StageRange::Nothing());
  } else if (joined == type::Err) {
    type = type::Err;  // There were no valid types anywhere in the array
    limit_to(StageRange::Nothing());
  } else {
    type = Arr(joined, elems_.size());
  }
}

void ArrayLiteral::Validate(Context *ctx) {
  STAGE_CHECK(StartBodyValidationStage, DoneBodyValidationStage);
  for (auto &elem : elems_) { elem->Validate(ctx); }
}

void ArrayLiteral::SaveReferences(Scope *scope, std::vector<IR::Val> *args) {
  for (auto &elem : elems_) { elem->SaveReferences(scope, args); }
}

void ArrayLiteral::contextualize(
    const Node *correspondant,
    const std::unordered_map<const Expression *, IR::Val> &replacements) {
  for (size_t i = 0; i < elems_.size(); ++i) {
    elems_[i]->contextualize(correspondant->as<ArrayLiteral>().elems_[i].get(),
                             replacements);
  }
}

void ArrayLiteral::ExtractReturns(std::vector<const Expression *> *rets) const {
  for (auto &el : elems_) { el->ExtractReturns(rets); }
}

ArrayLiteral *ArrayLiteral::Clone() const {
  auto *result = new ArrayLiteral;
  result->span = span;
  result->elems_.reserve(elems_.size());
  for (const auto &elem : elems_) {
    result->elems_.emplace_back(elem->Clone());
  }
  return result;
}

IR::Val AST::ArrayLiteral::EmitIR(Context *ctx) {
  // TODO If this is a constant we can just store it somewhere.
  auto array_val  = IR::Alloca(type);
  auto *data_type = type->as<type::Array>().data_type;
  for (size_t i = 0; i < elems_.size(); ++i) {
    type::EmitMoveInit(data_type, data_type, elems_[i]->EmitIR(ctx),
                       IR::Index(array_val, IR::Val::Int(static_cast<i32>(i))),
                       ctx);
  }
  return array_val;
}

}  // namespace AST