#include "module.h"

#include "ast/ast.h"
#include "ir/func.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "type/function.h"

// Can't declare this in header because unique_ptr's destructor needs to know
// the size of IR::Func which we want to forward declare.
Module::Module()
    : global_(std::make_unique<DeclScope>(nullptr)),
      llvm_ctx_(std::make_unique<llvm::LLVMContext>()),
      llvm_(std::make_unique<llvm::Module>("my module", *llvm_ctx_)) {
  global_->module_ = this;
}
Module::~Module() = default;

IR::Func* Module::AddFunc(
    const type::Function* fn_type,
    std::vector<std::pair<std::string, AST::Expression*>> args) {
  fns_.push_back(std::make_unique<IR::Func>(fn_type, std::move(args)));
  fns_.back()->llvm_fn_ =
      llvm::Function::Create(fn_type->llvm_fn(*llvm_ctx_),
                             llvm::Function::ExternalLinkage, "", llvm_.get());
  fns_.back()->llvm_fn_->setName(fns_.back()->name());
  return fns_.back().get();
}

const type::Type* Module::GetType(const std::string& name) const {
  auto* decl = GetDecl(name);
  if (decl == nullptr) { return nullptr; }
  return decl->type;
}

AST::Declaration* Module::GetDecl(const std::string& name) const {
  for (const auto& stmt : statements_.content_) {
    if (!stmt->is<AST::Declaration>()) { continue; }
    const auto& id = stmt->as<AST::Declaration>().identifier->token;
    if (id != name) { continue; }
    return &stmt->as<AST::Declaration>();
  }
  return nullptr;
}

void Module::Complete() {
  while (!to_complete_.empty()) {
    auto* fn_lit = to_complete_.front();
    fn_lit->CompleteBody(this);
    to_complete_.pop();
  }
}