#include "Type.h"
#include "Scope.h"

extern llvm::Module* global_module;

namespace cstdlib {
  extern llvm::Constant* free();
}  // namespace cstdlib

namespace data {
  extern llvm::Value* const_char(char c);
  extern llvm::Value* const_uint(size_t n);
  extern llvm::Value* const_neg(llvm::IRBuilder<>& bldr, size_t n);
}  // namespace data

extern llvm::BasicBlock* make_block(const std::string& name, llvm::Function* fn);
extern llvm::Module* global_module;

namespace TypeSystem {
  void Primitive::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {}
}  // namespace TypeSystem

void Array::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {
  if (uninit_fn_ == nullptr) {
    uninit_fn_ = llvm::Function::Create(
        llvm::FunctionType::get(*Void, { *Ptr(this) }, false),
        llvm::Function::ExternalLinkage, "uninit." + to_string(), global_module);

    FnScope* fn_scope = Scope::build_fn<FnScope>();
    fn_scope->set_parent_function(uninit_fn_);
    fn_scope->set_type(get_function(Ptr(this), Void));

    llvm::IRBuilder<>& fnbldr = fn_scope->builder();
    fn_scope->enter();

    auto alloc = uninit_fn_->args().begin();
    auto data_ptr = fnbldr.CreateLoad(alloc);
    auto ptr_to_free = fnbldr.CreateGEP(fnbldr.CreateBitCast(data_ptr, *RawPtr),
        { data::const_neg(fnbldr, Uint->bytes()) }, "ptr_to_free");

    if (data_type()->requires_uninit()) {
      auto len_ptr = fnbldr.CreateBitCast(ptr_to_free, *Ptr(Uint), "len_ptr");
      auto len_val = fnbldr.CreateLoad(len_ptr);
      auto end_ptr = fnbldr.CreateGEP(data_ptr, { len_val });

      auto loop_block = make_block("loop", uninit_fn_);

      fnbldr.CreateBr(loop_block);
      fnbldr.SetInsertPoint(loop_block);

      auto phi = fnbldr.CreatePHI(*Ptr(data_type()), 2, "phi");
      phi->addIncoming(data_ptr, fn_scope->entry_block());

      data_type()->call_uninit(fnbldr, { phi });
      auto next_ptr = fnbldr.CreateGEP(phi, { data::const_uint(1) });

      fnbldr.CreateCondBr(fnbldr.CreateICmpULT(next_ptr, end_ptr),
          loop_block, fn_scope->exit_block());
      phi->addIncoming(next_ptr, loop_block);
    }

    fnbldr.CreateCall(cstdlib::free(), { ptr_to_free });

    fn_scope->exit();
  }

  bldr.CreateCall(uninit_fn_, { var });
}

void Tuple::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {
  // TODO
}

void Pointer::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {}
void Function::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {}
void Enum::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {}

void UserDefined::call_uninit(llvm::IRBuilder<>& bldr, llvm::Value* var) {
  if (!requires_uninit()) return;

  if (uninit_fn_ == nullptr) {
    uninit_fn_ = llvm::Function::Create(Type::get_function(Ptr(this), Void)->llvm(),
        llvm::Function::ExternalLinkage, "uninit." + to_string(), global_module);

    auto block = make_block("entry", uninit_fn_);

    llvm::IRBuilder<> fnbldr(llvm::getGlobalContext());
    fnbldr.SetInsertPoint(block);

    auto fields_size = fields_.size();
    for (size_t field_num = 0; field_num < fields_size; ++field_num) {
      auto field_type = fields_[field_num].second;
      if (field_type->requires_uninit()) {
        auto arg = fnbldr.CreateGEP(uninit_fn_->args().begin(),
            { data::const_uint(0), data::const_uint(field_num) });
        field_type->call_uninit(fnbldr, { arg });
      }
    }

    fnbldr.CreateRetVoid();
  }

  bldr.CreateCall(uninit_fn_, { var });
}
