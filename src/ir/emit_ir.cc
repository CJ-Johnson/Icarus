#include "ir.h"

#include <vector>

#include "../ast/ast.h"
#include "../error_log.h"
#include "../scope.h"
#include "../type/type.h"

#define VERIFY_OR_EXIT                                                         \
  do {                                                                         \
    verify_types();                                                            \
    if (ErrorLog::NumErrors() != 0) { return IR::Val::None(); }                \
  } while (false)

extern IR::Val Evaluate(AST::Expression *expr);
extern std::vector<IR::Val> global_vals;

// If the expression is a CommaList, apply the function to each expr. Otherwise
// call it on the expression itself.
void ForEachExpr(AST::Expression *expr,
                 const std::function<void(size_t, AST::Expression *)> &fn) {
  if (expr->is<AST::CommaList>()) {
    const auto &exprs = expr->as<AST::CommaList>().exprs;
    for (size_t i = 0; i < exprs.size(); ++i) { fn(i, exprs[i].get()); }
  } else {
    fn(0, expr);
  }
}

IR::Val ErrorFunc() {
  static IR::Func *ascii_func_ = []() {
    auto fn = new IR::Func(Func(String, Code), {{"", nullptr}});
    CURRENT_FUNC(fn) {
      IR::Block::Current = fn->entry();
      // TODO
      IR::SetReturn(IR::ReturnValue{0}, IR::Val::CodeBlock(nullptr));
      IR::ReturnJump();
    }
    fn->name = "error";
    return fn;
  }();
  return IR::Val::Func(ascii_func_);
}

IR::Val AsciiFunc() {
  static IR::Func *ascii_func_ = []() {
    auto fn = new IR::Func(Func(Uint, Char), {{"", nullptr}});
    CURRENT_FUNC(fn) {
      IR::Block::Current = fn->entry();
      IR::SetReturn(IR::ReturnValue{0}, IR::Trunc(fn->Argument(0)));
      IR::ReturnJump();
    }
    fn->name = "ascii";
    return fn;
  }();
  return IR::Val::Func(ascii_func_);
}

IR::Val OrdFunc() {
  static IR::Func *ord_func_ = []() {
    auto fn = new IR::Func(Func(Char, Uint), {{"", nullptr}});
    CURRENT_FUNC(fn) {
      IR::Block::Current = fn->entry();
      IR::SetReturn(IR::ReturnValue{0}, IR::Extend(fn->Argument(0)));
      IR::ReturnJump();
    }
    fn->name = "ord";
    return fn;
  }();
  return IR::Val::Func(ord_func_);
}

IR::Val AST::Access::EmitLVal(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;

  auto val = operand->EmitLVal(kind);
  while (val.type->is<Pointer>() &&
         !val.type->as<Pointer>().pointee->is_big()) {
    val = IR::Load(val);
  }

  ASSERT_TYPE(Pointer, val.type);
  ASSERT_TYPE(Struct, val.type->as<Pointer>().pointee);

  auto *struct_type = &val.type->as<Pointer>().pointee->as<Struct>();
  return IR::Field(val, struct_type->field_name_to_num AT(member_name));
}

// TODO better name
static IR::Val VariantMatch(const IR::Val &needle, Type *haystack) {
  auto runtime_type = IR::Load(IR::VariantType(needle));

  if (haystack->is<Variant>()) {
    // TODO I'm fairly confident this will work, but it's also overkill because
    // we may already know this type matches if one variant is a subset of the
    // other.
    auto landing = IR::Func::Current->AddBlock();

    std::vector<IR::Val> phi_args;
    phi_args.reserve(2 * haystack->as<Variant>().size() + 2);
    for (Type *v : haystack->as<Variant>().variants_) {
      phi_args.push_back(IR::Val::Block(IR::Block::Current));
      phi_args.push_back(IR::Val::Bool(true));

      IR::Block::Current =
          IR::EarlyExitOn<true>(landing, IR::Eq(IR::Val::Type(v), runtime_type));
    }

    phi_args.push_back(IR::Val::Block(IR::Block::Current));
    phi_args.push_back(IR::Val::Bool(false));
    IR::UncondJump(landing);

    IR::Block::Current = landing;
    auto phi           = IR::Phi(Bool);
    IR::Func::Current->SetArgs(phi, std::move(phi_args));

    return IR::Func::Current->Command(phi).reg();

  } else {
    // TODO actually just implicitly convertible to haystack
    return IR::Eq(IR::Val::Type(haystack), runtime_type);
  }
}

IR::Val AST::Call::EmitIR(IR::Cmd::Kind kind) {
  // Look at all the possible calls and generate the dispatching code
  // TODO implement this with a lookup table instead of this branching
  // insanity.

  // TODO an opmitimazion we can do is merging all the allocas for results into
  // a single variant buffer, because we know we need something that big anyway,
  // and their use cannot overlap.
  auto landing_block = IR::Func::Current->AddBlock();

  std::unordered_map<Expression *, IR::Val *> expr_map;
  std::vector<IR::Val> pos_vals;
  pos_vals.reserve(pos_.size());
  for (auto &expr : pos_) {
    pos_vals.push_back(expr->type->is_big() ? PtrCallFix(expr->EmitIR(kind))
                                            : expr->EmitIR(kind));
    expr_map[expr.get()] = &pos_vals.back(); // Safe because of reserve.
  }

  std::unordered_map<std::string, IR::Val> named_vals;
  for (auto & [ name, expr ] : named_) {
    auto[iter, success] = named_vals.emplace(
        name, expr->type->is_big() ? PtrCallFix(expr->EmitIR(kind))
                                   : expr->EmitIR(kind));
    expr_map[expr.get()] = &iter->second;
  }

  std::vector<IR::Val> results;
  results.reserve(2 * dispatch_table_.bindings_.size());

  // TODO deal with dispatching named arguments.
  for (const auto & [ call_arg_type, binding ] : dispatch_table_.bindings_) {
    auto next_binding = IR::Func::Current->AddBlock();
    // Generate code that attempts to match the types on each argument (only
    // check the ones at the call-site that could be variants).
    for (size_t i = 0; i < pos_.size(); ++i) {
      if (!pos_[i]->type->is<Variant>()) { continue; }
      IR::Block::Current = IR::EarlyExitOn<false>(
          next_binding, VariantMatch(pos_vals[i], call_arg_type.pos_[i]));
    }

    for (auto & [ name, expr ] : named_) {
      auto iter = call_arg_type.find(name);
      if (iter == call_arg_type.named_.end()) { continue; }
      if (!expr->type->is<Variant>()) { continue; }
      IR::Block::Current = IR::EarlyExitOn<false>(
          next_binding, VariantMatch(named_vals[iter->first], iter->second));
    }

    // After the last check, if you pass, you should dispatch
    auto fn_to_call = binding.decl_->identifier->EmitIR(kind);
    std::vector<IR::Val> args;
    args.resize(binding.exprs_.size());
    for (size_t i = 0; i < args.size(); ++i) {
      auto[bound_type, expr] = binding.exprs_[i];
      if (expr == nullptr) {
        ASSERT_NE(bound_type, nullptr);
        auto default_expr =
            std::get<IR::Func *>(fn_to_call.value)->args_[i].second;
        args[i] = bound_type->PrepareArgument(default_expr->type,
                                              default_expr->EmitIR(kind));
      } else {
        args[i] = bound_type->PrepareArgument(expr->type, *expr_map[expr]);
      }
    }

    Type *output_type_for_this_binding =
        Tup(std::get<IR::Func *>(fn_to_call.value)->type_->output);
    IR::Val ret_val;
    if (output_type_for_this_binding->is_big()) {
      ret_val = IR::Alloca(type);
      IR::Call(fn_to_call, std::move(args), {ret_val});
    } else {
      if (type->is_big()) {
        ret_val = IR::Alloca(type);
        type->EmitAssign(output_type_for_this_binding,
                         IR::Call(fn_to_call, std::move(args), {}), ret_val);
      } else {
        ret_val = IR::Call(fn_to_call, std::move(args), {});
      }
    }
    results.push_back(IR::Val::Block(IR::Block::Current));
    results.push_back(ret_val);

    IR::UncondJump(landing_block);

    IR::Block::Current = next_binding;
  }
  // TODO this very last block is not be reachable and should never be
  // generated.

  IR::UncondJump(landing_block);
  IR::Block::Current = landing_block;

  if (results.empty()) {
    return IR::Val::None();
  } else {
    auto phi = IR::Phi(type->is_big() ? Ptr(type) : type);
    IR::Func::Current->SetArgs(phi, std::move(results));
    return IR::Func::Current->Command(phi).reg();
  }
}

IR::Val AST::Access::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;

  if (type->is<Enum>()) {
    return type->as<Enum>().EmitLiteral(member_name);
  } else {
    return PtrCallFix(EmitLVal(kind));
  }
  return IR::Val::None();
}

IR::Val AST::Terminal::EmitIR(IR::Cmd::Kind) {
  VERIFY_OR_EXIT;
  return value;
}

IR::Val AST::Identifier::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  ASSERT(decl, "No decl for identifier \"" + token + "\"");

  if (auto *ret = std::get_if<IR::ReturnValue>(&decl->addr.value);
      ret && kind == IR::Cmd::Kind::PostCondition) {
    return IR::Val::Reg(
        IR::Register{static_cast<i32>(scope_->ContainingFnScope()
                                          ->fn_lit->type->as<Function>()
                                          .input.size() +
                                      ret->value)},
        type);
  }

  if (decl->const_ || decl->arg_val) {
    return decl->addr;
  } else if (decl->is<InDecl>()) {
    if (auto &in_decl = decl->as<InDecl>();
        in_decl.container->type->is<Array>()) {
      return PtrCallFix(EmitLVal(kind));
    } else {
      return decl->addr;
    }
  } else {
    return PtrCallFix(EmitLVal(kind));
  }
}

IR::Val AST::ArrayLiteral::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  auto array_val  = IR::Alloca(type);
  auto *data_type = type->as<Array>().data_type;
  for (size_t i = 0; i < elems.size(); ++i) {
    auto elem_i = IR::Index(array_val, IR::Val::Uint(i));
    Type::EmitMoveInit(data_type, data_type, elems[i]->EmitIR(kind), elem_i);
  }
  return array_val;
}

IR::Val AST::For::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;

  auto init       = IR::Func::Current->AddBlock();
  auto incr       = IR::Func::Current->AddBlock();
  auto phi        = IR::Func::Current->AddBlock();
  auto cond       = IR::Func::Current->AddBlock();
  auto body_entry = IR::Func::Current->AddBlock();
  auto exit       = IR::Func::Current->AddBlock();

  IR::UncondJump(init);

  std::vector<IR::Val> init_vals;
  init_vals.reserve(iterators.size());
  { // Init block
    IR::Block::Current = init;
    for (auto &decl : iterators) {
      if (decl->container->type->is<RangeType>()) {
        if (decl->container->is<Binop>()) {
          init_vals.push_back(decl->container->as<Binop>().lhs->EmitIR(kind));
        } else if (decl->container->is<Unop>()) {
          init_vals.push_back(
              decl->container->as<Unop>().operand->EmitIR(kind));
        } else {
          NOT_YET();
        }
      } else if (decl->container->type->is<Array>()) {
        init_vals.push_back(
            IR::Index(decl->container->EmitLVal(kind), IR::Val::Uint(0)));

      } else if (decl->container->type == Type_) {
        // TODO this conditional check on the line above is wrong
        IR::Val container_val = Evaluate(decl->container.get());
        if (::Type *t = std::get<::Type *>(container_val.value);
            t->is<Enum>()) {
          init_vals.push_back(t->as<Enum>().EmitInitialValue());
        } else {
          NOT_YET();
        }
      } else {
        NOT_YET();
      }
    }
    IR::UncondJump(phi);
  }

  std::vector<IR::CmdIndex> phis;
  phis.reserve(iterators.size());
  { // Phi block
    IR::Block::Current = phi;
    for (auto &decl : iterators) {
      if (decl->container->type == Type_) {
        IR::Val container_val = Evaluate(decl->container.get());
        if (::Type *t = std::get<::Type *>(container_val.value);
            t->is<Enum>()) {
          phis.push_back(IR::Phi(&t->as<Enum>()));
        } else {
          NOT_YET();
        }
      } else if (decl->container->type->is<RangeType>()) {
        phis.push_back(
            IR::Phi(decl->container->type->as<RangeType>().end_type));
      } else if (decl->container->type->is<Array>()) {
        phis.push_back(
            IR::Phi(Ptr(decl->container->type->as<Array>().data_type)));
      } else {
        NOT_YET(*decl->container->type);
      }
    }
    IR::UncondJump(cond);
  }

  std::vector<IR::Val> incr_vals;
  incr_vals.reserve(iterators.size());
  { // Incr block
    IR::Block::Current = incr;
    for (auto iter : phis) {
      auto phi_reg = IR::Func::Current->Command(iter).reg();
      if (phi_reg.type == Int) {
        incr_vals.push_back(IR::Add(phi_reg, IR::Val::Int(1)));
      } else if (phi_reg.type == Uint) {
        incr_vals.push_back(IR::Add(phi_reg, IR::Val::Uint(1)));
      } else if (phi_reg.type == Char) {
        incr_vals.push_back(IR::Add(phi_reg, IR::Val::Char(1)));
      } else if (phi_reg.type->is<Enum>()) {
        incr_vals.push_back(
            IR::Add(phi_reg, IR::Val::Enum(&phi_reg.type->as<Enum>(), 1)));
      } else if (phi_reg.type->is<Pointer>()) {
        incr_vals.push_back(IR::PtrIncr(phi_reg, IR::Val::Uint(1)));
      } else {
        NOT_YET();
      }
    }
    IR::UncondJump(phi);
  }

  { // Complete phi definition
    for (size_t i = 0; i < iterators.size(); ++i) {
      IR::Func::Current->SetArgs(phis[i], {IR::Val::Block(init), init_vals[i],
                                           IR::Val::Block(incr), incr_vals[i]});
      iterators[i]->addr = IR::Func::Current->Command(phis[i]).reg();
    }
  }

  { // Cond block
    IR::Block::Current = cond;
    for (size_t i = 0; i < iterators.size(); ++i) {
      auto *decl = iterators[i].get();
      auto reg   = IR::Func::Current->Command(phis[i]).reg();
      auto next  = IR::Func::Current->AddBlock();
      IR::Val cmp;
      if (decl->container->type->is<RangeType>()) {
        if (decl->container->is<Binop>()) {
          auto rhs_val = decl->container->as<Binop>().rhs->EmitIR(kind);
          cmp = IR::Le(reg, rhs_val);
        } else if (decl->container->is<Unop>()) {
          // TODO we should optimize this here rather then generate suboptimal
          // code and trust optimizations later on.
          cmp = IR::Val::Bool(true);
        } else {
          NOT_YET();
        }
      } else if (decl->container->type->is<Array>()) {
        auto *array_type = &decl->container->type->as<Array>();
        cmp = IR::Ne(reg, IR::Index(decl->container->EmitLVal(kind),
                                    IR::Val::Uint(array_type->len)));
      } else if (decl->container->type == Type_) {
        IR::Val container_val = Evaluate(decl->container.get());
        if (::Type *t = std::get<::Type *>(container_val.value);
            t->is<Enum>()) {
          cmp = IR::Le(reg, IR::Val::Enum(&t->as<Enum>(),
                                          t->as<Enum>().members.size() - 1));
        } else {
          NOT_YET();
        }
      } else {
        NOT_YET();
      }

      IR::CondJump(cmp, next, exit);
      IR::Block::Current = next;
    }
    IR::UncondJump(body_entry);
  }

  { // Body
    IR::Block::Current = body_entry;

    for_scope->Enter();
    statements->EmitIR(kind);
    for_scope->Exit();

    IR::UncondJump(incr);
  }

  IR::Block::Current = exit;
  return IR::Val::None();
}

IR::Val AST::Case::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  auto land = IR::Func::Current->AddBlock();

  ASSERT(!key_vals.empty(), "");
  std::vector<IR::Val> phi_args;
  phi_args.reserve(2 * key_vals.size());
  for (size_t i = 0; i < key_vals.size() - 1; ++i) {
    auto compute = IR::Func::Current->AddBlock();
    auto next    = IR::Func::Current->AddBlock();

    auto val = key_vals[i].first->EmitIR(kind);
    IR::CondJump(val, compute, next);

    IR::Block::Current = compute;
    auto result = key_vals[i].second->EmitIR(kind);
    phi_args.push_back(IR::Val::Block(IR::Block::Current));
    phi_args.push_back(result);
    IR::UncondJump(land);

    IR::Block::Current = next;
  }

  // Last entry
  auto result = key_vals.back().second->EmitIR(kind);
  phi_args.push_back(IR::Val::Block(IR::Block::Current));
  phi_args.push_back(result);
  IR::UncondJump(land);

  IR::Block::Current = land;
  auto phi           = IR::Phi(type);
  IR::Func::Current->SetArgs(phi, std::move(phi_args));
  return IR::Func::Current->Command(phi).reg();
}

IR::Val AST::ScopeLiteral::EmitIR(IR::Cmd::Kind) {
  VERIFY_OR_EXIT;
  return IR::Val::Scope(this);
}

IR::Val AST::ScopeNode::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  IR::Val scope_expr_val = Evaluate(scope_expr.get());
  ASSERT_TYPE(Scope_Type, scope_expr_val.type);

  auto enter_fn = std::get<AST::ScopeLiteral *>(scope_expr_val.value)
                      ->enter_fn->init_val->EmitIR(kind);
  ASSERT_NE(enter_fn, IR::Val::None());
  auto exit_fn = std::get<AST::ScopeLiteral *>(scope_expr_val.value)
                     ->exit_fn->init_val->EmitIR(kind);
  ASSERT_NE(exit_fn, IR::Val::None());

  auto call_enter_result =
      IR::Call(enter_fn, expr ? std::vector<IR::Val>{expr->EmitIR(kind)}
                              : std::vector<IR::Val>{},
               {});
  auto land_block  = IR::Func::Current->AddBlock();
  auto enter_block = IR::Func::Current->AddBlock();

  IR::CondJump(call_enter_result, enter_block, land_block);

  IR::Block::Current = enter_block;
  stmts->EmitIR(kind);

  auto call_exit_result = IR::Call(exit_fn, {}, {});
  IR::CondJump(call_exit_result, enter_block, land_block);

  IR::Block::Current = land_block;
  return IR::Val::None();
}

IR::Val AST::Declaration::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;

  if (const_) {
    // TODO it's custom or default initialized. cannot be uninitialized. This
    // should be verified by the type system.
    if (IsCustomInitialized()) {
      addr = Evaluate(init_val.get());
    } else if (IsDefaultInitialized()) {
      // TODO if EmitInitialValue requires generating code, that would be bad.
      addr = type->EmitInitialValue();
    } else {
      UNREACHABLE();
    }
  } else if (scope_ == Scope::Global) {
    // TODO these checks actually overlap and could be simplified.
    if (IsUninitialized()) {
      global_vals.emplace_back();
      global_vals.back().type = type;
      addr = IR::Val::GlobalAddr(global_vals.size() - 1, type);
    } else if (IsCustomInitialized()) {
      global_vals.push_back(Evaluate(init_val.get()));
      addr = IR::Val::GlobalAddr(global_vals.size() - 1, type);

    } else if (IsDefaultInitialized()) {
      // TODO if EmitInitialValue requires generating code, that would be bad.
      global_vals.push_back(type->EmitInitialValue());
      addr = IR::Val::GlobalAddr(global_vals.size() - 1, type);

    } else if (IsInferred()) {
      NOT_YET();

    } else {
      UNREACHABLE();
    }
  } else {
    // For local variables the declaration determines where the initial value is
    // set, but the allocation has to be done much earlier. We do the allocation
    // in FunctionLiteral::EmitIR. Declaration::EmitIR is just used to set the
    // value.
    ASSERT_NE(addr, IR::Val::None());
    ASSERT(scope_->ContainingFnScope(), "");

    // TODO these checks actually overlap and could be simplified.
    if (IsUninitialized()) { return IR::Val::None(); }
    if (IsCustomInitialized()) {
      lrvalue_check();
      if (init_val->lvalue == Assign::RVal) {
        Type::EmitMoveInit(init_val->type, type, init_val->EmitIR(kind), addr);
      } else {
        Type::EmitCopyInit(init_val->type, type, init_val->EmitIR(kind), addr);
      }
    } else {
      type->EmitInit(addr);
    }
  }

  return IR::Val::None();
}

IR::Val AST::Unop::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;

  switch (op) {
  case Language::Operator::Not:
  case Language::Operator::Sub: {
    return IR::Neg(operand->EmitIR(kind));
  } break;
  case Language::Operator::Return: {
    ForEachExpr(operand.get(), [kind](size_t i, AST::Expression *expr) {
      IR::SetReturn(IR::ReturnValue{static_cast<i32>(i)}, expr->EmitIR(kind));
    });
    IR::ReturnJump();
    return IR::Val::None();
  }
  case Language::Operator::Print: {
    ForEachExpr(operand.get(), [kind](size_t, AST::Expression *expr) {
      if (expr->type->is<Primitive>() || expr->type->is<Pointer>()) {
        IR::Print(expr->EmitIR(kind));
      } else {
        expr->type->EmitRepr(expr->EmitIR(kind));
      }
    });

    return IR::Val::None();
  } break;
  case Language::Operator::And: return operand->EmitLVal(kind);
  case Language::Operator::Eval:
    // TODO what if there's an error during evaluation?
    return Evaluate(operand.get());
  case Language::Operator::Generate: {
    auto val = Evaluate(operand.get());
    ASSERT_EQ(val.type, Code);
    std::get<AST::CodeBlock *>(val.value)->stmts->assign_scope(scope_);
    std::get<AST::CodeBlock *>(val.value)->stmts->EmitIR(kind);
    return IR::Val::None();
  } break;
  case Language::Operator::Mul: return IR::Ptr(operand->EmitIR(kind));
  case Language::Operator::At: return PtrCallFix(operand->EmitIR(kind));
  case Language::Operator::Needs: {
    // TODO validate requirements are well-formed?
    IR::Func::Current->preconditions_.push_back(operand.get());
    return IR::Val::None();
  } break;
  case Language::Operator::Ensure: {
    // TODO validate requirements are well-formed?
    IR::Func::Current->postconditions_.push_back(operand.get());
    return IR::Val::None();
  } break;
  default: UNREACHABLE("Operator is ", static_cast<int>(op));
  }
}

IR::Val AST::Binop::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  switch (op) {
#define CASE(op_name)                                                          \
  case Language::Operator::op_name: {                                          \
    auto lhs_ir = lhs->EmitIR(kind);                                           \
    auto rhs_ir = rhs->EmitIR(kind);                                           \
    return IR::op_name(lhs_ir, rhs_ir);                                        \
  } break
    CASE(Add);
    CASE(Sub);
    CASE(Mul);
    CASE(Div);
    CASE(Mod);
    CASE(Arrow);
#undef CASE
  case Language::Operator::Cast: {
    ASSERT(!rhs->is<AST::CommaList>(), "");
    auto lhs_ir = lhs->EmitIR(kind);
    auto rhs_ir = rhs->EmitIR(kind);
    return IR::Cast(lhs_ir, rhs_ir);
  } break;
  case Language::Operator::Assign: {
    std::vector<Type *> lhs_types, rhs_types;
    std::vector<IR::Val> rhs_vals;
    ForEachExpr(rhs.get(),
                [&rhs_vals, &rhs_types, kind](size_t, AST::Expression *expr) {
                  rhs_vals.push_back(expr->EmitIR(kind));
                  rhs_types.push_back(expr->type);
                });

    std::vector<IR::Val> lhs_lvals;
    ForEachExpr(lhs.get(),
                [&lhs_lvals, &lhs_types, kind](size_t, AST::Expression *expr) {
                  lhs_lvals.push_back(expr->EmitLVal(kind));
                  lhs_types.push_back(expr->type);
                });

    ASSERT_EQ(lhs_lvals.size(), rhs_vals.size());
    for (size_t i = 0; i < lhs_lvals.size(); ++i) {
      lhs_types[i]->EmitAssign(rhs_types[i], PtrCallFix(rhs_vals[i]),
                               lhs_lvals[i]);
    }
    return IR::Val::None();
  } break;
  case Language::Operator::OrEq: {
    auto land_block = IR::Func::Current->AddBlock();
    auto more_block = IR::Func::Current->AddBlock();

    auto lhs_val       = lhs->EmitIR(kind);
    auto lhs_end_block = IR::Block::Current;
    IR::CondJump(lhs_val, land_block, more_block);

    IR::Block::Current = more_block;
    auto rhs_val       = rhs->EmitIR(kind);
    auto rhs_end_block = IR::Block::Current;
    IR::UncondJump(land_block);

    IR::Block::Current = land_block;

    auto phi = IR::Phi(Bool);
    IR::Func::Current->SetArgs(phi, {IR::Val::Block(lhs_end_block),
                                     IR::Val::Bool(true),
                                     IR::Val::Block(rhs_end_block), rhs_val});
    return IR::Func::Current->Command(phi).reg();
  } break;
  case Language::Operator::AndEq: {
    auto land_block = IR::Func::Current->AddBlock();
    auto more_block = IR::Func::Current->AddBlock();

    auto lhs_val       = lhs->EmitIR(kind);
    auto lhs_end_block = IR::Block::Current;
    IR::CondJump(lhs_val, more_block, land_block);

    IR::Block::Current = more_block;
    auto rhs_val       = rhs->EmitIR(kind);
    auto rhs_end_block = IR::Block::Current;
    IR::UncondJump(land_block);

    IR::Block::Current = land_block;

    auto phi = IR::Phi(Bool);
    IR::Func::Current->SetArgs(phi, {IR::Val::Block(lhs_end_block),
                                     IR::Val::Bool(false),
                                     IR::Val::Block(rhs_end_block), rhs_val});
    return IR::Func::Current->Command(phi).reg();
  } break;
#define CASE_ASSIGN_EQ(op_name)                                                \
  case Language::Operator::op_name##Eq: {                                      \
    auto lhs_lval = lhs->EmitLVal(kind);                                       \
    auto rhs_ir   = rhs->EmitIR(kind);                                         \
    IR::Store(IR::op_name(PtrCallFix(lhs_lval), rhs_ir), lhs_lval);            \
    return IR::Val::None();                                                    \
  } break
    CASE_ASSIGN_EQ(Xor);
    CASE_ASSIGN_EQ(Add);
    CASE_ASSIGN_EQ(Sub);
    CASE_ASSIGN_EQ(Mul);
    CASE_ASSIGN_EQ(Div);
    CASE_ASSIGN_EQ(Mod);
#undef CASE_ASSIGN_EQ
  case Language::Operator::Index: return PtrCallFix(EmitLVal(kind));
  default: UNREACHABLE(*this);
  }
}

IR::Val AST::ArrayType::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  return IR::Array(length->EmitIR(kind), data_type->EmitIR(kind));
}

static IR::Val EmitChainOpPair(Type *lhs_type, const IR::Val &lhs_ir,
                               Language::Operator op, Type *rhs_type,
                               const IR::Val &rhs_ir) {
  if (lhs_type->is<Array>() && rhs_type->is<Array>()) {
    ASSERT(op == Language::Operator::Eq || op == Language::Operator::Ne, "");
    return Array::Compare(&lhs_type->as<Array>(), lhs_ir,
                          &rhs_type->as<Array>(), rhs_ir,
                          op == Language::Operator::Eq);
  } else {
    switch (op) {
    case Language::Operator::Lt: return IR::Lt(lhs_ir, rhs_ir);
    case Language::Operator::Le: return IR::Le(lhs_ir, rhs_ir);
    case Language::Operator::Eq: return IR::Eq(lhs_ir, rhs_ir);
    case Language::Operator::Ne: return IR::Ne(lhs_ir, rhs_ir);
    case Language::Operator::Ge: return IR::Ge(lhs_ir, rhs_ir);
    case Language::Operator::Gt:
      return IR::Gt(lhs_ir, rhs_ir);
    // TODO case Language::Operator::And: cmp = lhs_ir; break;

    default: UNREACHABLE();
    }
  }
}

IR::Val AST::ChainOp::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  if (ops[0] == Language::Operator::Xor) {
    auto val = IR::Val::Bool(false);
    for (const auto &expr : exprs) { val = IR::Xor(val, expr->EmitIR(kind)); }
    return val;
  } else if (ops[0] == Language::Operator::Or && type == Type_) {
    // TODO probably want to check that each expression is a type? What if I
    // overload | to take my own stuff and have it return a type?
    std::vector<IR::Val> args;
    args.reserve(exprs.size());
    for (const auto &expr : exprs) { args.push_back(expr->EmitIR(kind)); }
    return IR::Variant(std::move(args));
  } else if (ops[0] == Language::Operator::And ||
             ops[0] == Language::Operator::Or) {
    auto land_block = IR::Func::Current->AddBlock();
    std::vector<IR::Val> phi_args;
    phi_args.reserve(2 * exprs.size());
    bool is_or = (ops[0] == Language::Operator::Or);
    for (size_t i = 0; i < exprs.size() - 1; ++i) {
      auto val = exprs[i]->EmitIR(kind);

      auto next_block = IR::Func::Current->AddBlock();
      IR::CondJump(val, is_or ? land_block : next_block,
                   is_or ? next_block : land_block);
      phi_args.push_back(IR::Val::Block(IR::Block::Current));
      phi_args.push_back(IR::Val::Bool(is_or));

      IR::Block::Current = next_block;
    }

    phi_args.push_back(IR::Val::Block(IR::Block::Current));
    phi_args.push_back(exprs.back()->EmitIR(kind));
    IR::UncondJump(land_block);

    IR::Block::Current = land_block;
    auto phi           = IR::Phi(Bool);
    IR::Func::Current->SetArgs(phi, std::move(phi_args));
    return IR::Func::Current->Command(phi).reg();

  } else {
    if (ops.size() == 1) {
      auto lhs_ir = exprs[0]->EmitIR(kind);
      auto rhs_ir = exprs[1]->EmitIR(kind);
      return EmitChainOpPair(exprs[0]->type, lhs_ir, ops[0], exprs[1]->type,
                             rhs_ir);

    } else {
      std::vector<IR::Val> phi_args;
      auto lhs_ir     = exprs.front()->EmitIR(kind);
      auto land_block = IR::Func::Current->AddBlock();
      for (size_t i = 0; i < ops.size() - 1; ++i) {
        auto rhs_ir = exprs[i + 1]->EmitIR(kind);
        IR::Val cmp = EmitChainOpPair(exprs[i]->type, lhs_ir, ops[i],
                                      exprs[i + 1]->type, rhs_ir);

        phi_args.push_back(IR::Val::Block(IR::Block::Current));
        phi_args.push_back(IR::Val::Bool(false));
        auto next_block = IR::Func::Current->AddBlock();
        IR::CondJump(cmp, next_block, land_block);
        IR::Block::Current = next_block;
        lhs_ir             = rhs_ir;
      }

      // Once more for the last element, but don't do a conditional jump.
      auto rhs_ir = exprs.back()->EmitIR(kind);
      auto last_cmp =
          EmitChainOpPair(exprs[exprs.size() - 2]->type, lhs_ir, ops.back(),
                          exprs[exprs.size() - 1]->type, rhs_ir);
      phi_args.push_back(IR::Val::Block(IR::Block::Current));
      phi_args.push_back(last_cmp);
      IR::UncondJump(land_block);

      IR::Block::Current = land_block;
      auto phi           = IR::Phi(Bool);
      IR::Func::Current->SetArgs(phi, std::move(phi_args));
      return IR::Func::Current->Command(phi).reg();
    }
  }
}

IR::Val AST::CommaList::EmitIR(IR::Cmd::Kind) { UNREACHABLE(this); }
IR::Val AST::CommaList::EmitLVal(IR::Cmd::Kind) { NOT_YET(); }

IR::Val AST::FunctionLiteral::EmitTemporaryIR(IR::Cmd::Kind kind) {
  return EmitIRAndSave(false, kind);
}
IR::Val AST::FunctionLiteral::EmitIR(IR::Cmd::Kind) {
  return EmitIRAndSave(true, IR::Cmd::Kind::Exec);
}

IR::Val AST::FunctionLiteral::EmitIRAndSave(bool should_save,
                                            IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  // Verifying 'this' only verifies the declared functions type not the
  // internals. We need to do that here.
  statements->verify_types();
  if (ErrorLog::NumErrors() != 0) { return IR::Val::None(); }

  if (!ir_func) {
    std::vector<std::pair<std::string, AST::Expression *>> args;
    args.reserve(inputs.size());
    for (const auto &input : inputs) {
      args.emplace_back(input->as<Declaration>().identifier->token,
                        input->as<Declaration>().init_val.get());
    }

    std::vector<Type *> input_types;
    for (Type *in : type->as<Function>().input) {
      input_types.push_back(in->is_big() ? Ptr(in) : in);
    }
    Function *fn_type =
        Func(std::move(input_types), type->as<Function>().output);

    if (should_save) {
      IR::Func::All.push_back(
          std::make_unique<IR::Func>(fn_type, std::move(args)));
      ir_func = IR::Func::All.back().get();
    } else {
      // TODO XXX This is SUPER DANGEROUS! Depending on a bool passed in we
      // either own or don't own?!?!?!
      ir_func = new IR::Func(fn_type, std::move(args));
    }

    CURRENT_FUNC(ir_func) {
      IR::Block::Current = ir_func->entry();
      // Leave space for allocas that will come later (added to the entry
      // block).
      auto start_block   = IR::Func::Current->AddBlock();
      IR::Block::Current = start_block;

      for (size_t i = 0; i < inputs.size(); ++i) {
        auto &arg = inputs[i];
        ASSERT_EQ(arg->addr, IR::Val::None());
        // TODO This whole loop can be done on construction!
        arg->addr = IR::Func::Current->Argument(static_cast<i32>(i));
      }

      // TODO multiple return types
      if (return_type_expr->is<Declaration>()) {
        return_type_expr->as<Declaration>().addr =
            IR::Val::Ret(0, return_type_expr->type);
      }

      for (auto scope : fn_scope->innards_) {
        scope->ForEachDeclHere([kind, scope](Declaration *decl) {
          // TODO arg_val seems to go along with in_decl a lot. Is there some
          // reason for this that *should* be abstracted?
          if (decl->arg_val || decl->is<InDecl>()) { return; }
          ASSERT_NE(decl->type, nullptr);
          decl->addr = IR::Alloca(decl->type);
        });
      }

      statements->EmitIR(kind);
      IR::ReturnJump();

      IR::Block::Current = ir_func->entry();
      IR::UncondJump(start_block);
    }
  }

  return IR::Val::Func(ir_func);
}

IR::Val AST::Statements::EmitIR(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  for (auto &stmt : statements) { stmt->EmitIR(kind); }
  return IR::Val::None();
}

IR::Val AST::CodeBlock::EmitIR(IR::Cmd::Kind) {
  VERIFY_OR_EXIT;
  std::vector<IR::Val> args;
  stmts->contextualize(scope_, &args);
  return IR::Contextualize(this, std::move(args));
}

IR::Val AST::Identifier::EmitLVal(IR::Cmd::Kind kind) {
  VERIFY_OR_EXIT;
  ASSERT_NE(decl, nullptr);

  if (decl->addr == IR::Val::None()) { decl->EmitIR(kind); }
  // TODO kind???

  return decl->addr;
}

IR::Val AST::Unop::EmitLVal(IR::Cmd::Kind kind) {
  switch (op) {
  case Language::Operator::At: return operand->EmitIR(kind);
  default: UNREACHABLE("Operator is ", static_cast<int>(op));
  }
}

IR::Val AST::Binop::EmitLVal(IR::Cmd::Kind kind) {
  switch (op) {
  case Language::Operator::Index:
    if (lhs->type->is<::Array>()) {
      return IR::Index(lhs->EmitLVal(kind), rhs->EmitIR(kind));
    }
    [[fallthrough]];
  default: UNREACHABLE("Operator is ", static_cast<int>(op));
  }
}
