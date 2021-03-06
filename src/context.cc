#include "context.h"
#include "ast/expression.h"

type::Type const *Context::type_of(ast::Expression const *expr) const {
  if (auto *result = mod_->type_of(bound_constants_, expr)) { return result; }
  if (parent_) { return parent_->type_of(expr); }

  // When searching in embedded modules we intentionally look with no bound
  // constants. Across module boundaries, a declaration can't be present anyway.
  for (Module const *mod : mod_->global_->embedded_modules_) {
    auto bc_iter = mod->data_.find(ast::BoundConstants{});
    if (bc_iter == mod->data_.end()) { continue; }
    auto iter = bc_iter->second.types_.data_.find(expr);
    if (iter != bc_iter->second.types_.data_.end()) { return iter->second; }
  }
  return nullptr;
}

type::Type const *Context::set_type(ast::Expression const *expr,
                                    type::Type const *t) {
  return mod_->set_type(bound_constants_, expr, ASSERT_NOT_NULL(t));
}

void Context::set_addr(ast::Declaration *decl, ir::Register r) {
  mod_->data_[bound_constants_].addr_[decl] = r;
}

ir::Register Context::addr(ast::Declaration *decl) const {
  return mod_->addr(bound_constants_, decl);
}

void Context::set_dispatch_table(ast::Expression const *expr,
                                 ast::DispatchTable &&table) {
  ASSERT(mod_->data_[bound_constants_]
             .dispatch_tables_.emplace(expr, std::move(table))
             .second);
}

ast::DispatchTable const *Context::dispatch_table(ast::Expression const *expr) const {
  auto &table = mod_->data_[bound_constants_].dispatch_tables_;
  if (auto iter = table.find(expr); iter != table.end()) {
    return &iter->second;
  }
  if (parent_) { return parent_->dispatch_table(expr); }
  return nullptr;
}

void Context::push_rep_dispatch_table(ast::Node const *node,
                                      ast::DispatchTable &&tables) {
  mod_->data_[bound_constants_].repeated_dispatch_tables_[node].push_back(
      std::move(tables));
}

base::vector<ast::DispatchTable> const *Context::rep_dispatch_tables(
    ast::Node const *node) const {
  auto &table = mod_->data_[bound_constants_].repeated_dispatch_tables_;
  if (auto iter = table.find(node); iter != table.end()) {
    return &iter->second;
  }
  if (parent_) { return parent_->rep_dispatch_tables(node); }
  return nullptr;
}
