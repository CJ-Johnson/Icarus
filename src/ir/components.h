#ifndef ICARUS_IR_COMPONENTS_H
#define ICARUS_IR_COMPONENTS_H

#include <tuple>

#include "base/container/tuple.h"
#include "ir/cmd.h"
#include "ir/func.h"
#include "ir/phi.h"
#include "ir/register.h"

namespace ir {
template <bool B>
BlockIndex EarlyExitOn(BlockIndex exit_block, RegisterOr<bool> cond) {
  auto continue_block = Func::Current->AddBlock();
  if constexpr (B) {
    CondJump(cond, exit_block, continue_block);
  } else {
    CondJump(cond, continue_block, exit_block);
  }
  return continue_block;
}

template <typename LoopPhiFn, typename LoopBodyFn, typename TypeTup,
          typename... Ts>
auto CreateLoop(LoopPhiFn &&loop_phi_fn, LoopBodyFn &&loop_body_fn,
                TypeTup &&types, std::tuple<ir::RegisterOr<Ts>...> entry_vals) {
  auto entry_block = ir::BasicBlock::Current;

  auto loop_phi   = ir::Func::Current->AddBlock();
  auto loop_body  = ir::Func::Current->AddBlock();
  auto exit_block = ir::Func::Current->AddBlock();

  ir::UncondJump(loop_phi);
  ir::BasicBlock::Current = loop_phi;

  auto phi_indices = base::tuple::transform(ir::Phi, types);
  auto phi_vals    = base::tuple::transform(
      [](auto &&val) { return ir::Func::Current->Command(val).result; },
      phi_indices);

  auto exit_cond = std::forward<LoopPhiFn>(loop_phi_fn)(phi_vals);
  ir::CondJump(exit_cond, exit_block, loop_body);

  ir::BasicBlock::Current = loop_body;
  auto new_phis           = std::forward<LoopBodyFn>(loop_body_fn)(phi_vals);
  ir::UncondJump(loop_phi);

  base::tuple::for_each(
      [&](auto &&phi_index, auto &&entry_val, auto &&new_phi) {
        using T = std::decay_t<decltype(entry_val.val_)>;
        ir::MakePhi<T>(
            phi_index,
            std::unordered_map<BlockIndex, ir::RegisterOr<T>>{
                std::pair<BlockIndex, ir::RegisterOr<T>>{entry_block,
                                                         entry_val},
                std::pair<BlockIndex, ir::RegisterOr<T>>{loop_body, new_phi}});
      },
      std::move(phi_indices), std::move(entry_vals), std::move(new_phis));

  ir::BasicBlock::Current = exit_block;
  return phi_vals;
}

inline Register PtrFix(Register r, type::Type const *desired_type) {
  return desired_type->is_big() ? r : Load(r, desired_type);
}
}  // namespace ir

#endif  // ICARUS_IR_COMPONENTS_H
