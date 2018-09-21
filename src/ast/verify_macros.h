#ifndef ICARUS_AST_VERIFY_MACROS_H
#define ICARUS_AST_VERIFY_MACROS_H

#include "ast/declaration.h"
#include "ast/stages.h"
#include "context.h"

namespace type {
extern Type const *Err;
}  // namespace type

#define HANDLE_CYCLIC_DEPENDENCIES                                             \
  do {                                                                         \
    if (ctx->cyc_dep_vec_ == nullptr) { break; }                               \
    if constexpr (std::is_same_v<decltype(this), Identifier *>) {              \
      auto *this_as_id = reinterpret_cast<Identifier *>(this);                 \
      if (!ctx->cyc_dep_vec_->empty() &&                                       \
          this_as_id == ctx->cyc_dep_vec_->front()) {                          \
        ctx->cyc_dep_vec_ = nullptr;                                           \
      } else {                                                                 \
        ctx->cyc_dep_vec_->push_back(this_as_id);                              \
      }                                                                        \
      ASSERT(ctx->types_.emplace(this, type::Err).second);                     \
    } else if constexpr (std::is_same_v<decltype(this), Declaration *>) {      \
      auto *this_as_decl =                                                     \
          reinterpret_cast<Declaration *>(this)->identifier.get();             \
      if (!ctx->cyc_dep_vec_->empty() &&                                       \
          this_as_decl == ctx->cyc_dep_vec_->front()) {                        \
        ctx->cyc_dep_vec_ = nullptr;                                           \
      } else {                                                                 \
        ctx->cyc_dep_vec_->push_back(this_as_decl);                            \
      }                                                                        \
      ASSERT(ctx->types_.emplace(this, type::Err).second);                     \
    } else {                                                                   \
      ctx->types_.buffered_emplace(this, type::Err);                           \
    }                                                                          \
    type = type::Err;                                                          \
    limit_to(StageRange::Nothing());                                           \
    return;                                                                    \
  } while (false)

#define VERIFY_STARTING_CHECK_EXPR                                             \
  base::defer defer_##__LINE__(                                                \
      [this]() { this->stage_range_.low = DoneTypeVerificationStage; });       \
  if (stage_range_.high < StartTypeVerificationStage) { return; }              \
  if (stage_range_.low >= DoneTypeVerificationStage) { return; }               \
  if (stage_range_.low == StartTypeVerificationStage) {                        \
    ctx->cyc_dep_vec_ = ctx->error_log_.CyclicDependency();                    \
    HANDLE_CYCLIC_DEPENDENCIES;                                                \
  }                                                                            \
  stage_range_.low = StartTypeVerificationStage

#define VERIFY_OR_RETURN(expr_type, expr)                                      \
  type::Type const *expr_type = nullptr;                                       \
  do {                                                                         \
    expr->VerifyType(ctx);                                                     \
    HANDLE_CYCLIC_DEPENDENCIES;                                                \
    expr_type = expr->type;                                                    \
    if (expr->type == type::Err) {                                             \
      type = type::Err;                                                        \
      /* TODO Maybe this should be Nothing() */                                \
      limit_to(expr->stage_range_.high);                                       \
      return;                                                                  \
    }                                                                          \
  } while (false)

#endif  // ICARUS_AST_VERIFY_MACROS_H
