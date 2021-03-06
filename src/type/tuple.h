#ifndef ICARUS_TYPE_TUPLE_H
#define ICARUS_TYPE_TUPLE_H

#include "type.h"

struct Architecture;

namespace type {
struct Tuple : public Type {
  Tuple() = delete;
  ~Tuple() {}
  Tuple(base::vector<Type const *> entries) : entries_(std::move(entries)) {}
  void WriteTo(std::string *result) const override;

  void EmitCopyAssign(Type const *from_type, ir::Val const &from,
                      ir::RegisterOr<ir::Addr> to, Context *ctx) const override;
  void EmitMoveAssign(Type const *from_type, ir::Val const &from,
                      ir::RegisterOr<ir::Addr> to, Context *ctx) const override;
  virtual void EmitInit(ir::Register reg, Context *ctx) const;
  virtual void EmitDestroy(ir::Register reg, Context *ctx) const;
  virtual ir::Val PrepareArgument(Type const *t, ir::Val const &val,
                                  Context *ctx) const;

  virtual void EmitRepr(ir::Val const &id_val, Context *ctx) const;

  virtual Cmp Comparator() const { UNREACHABLE(); }

  virtual void defining_modules(
      std::unordered_set<::Module const *> *modules) const;

  size_t offset(size_t n, Architecture const &arch) const;

  bool IsCopyable() const override;
  bool IsMovable() const override;

  Type const *finalize();

#ifdef ICARUS_USE_LLVM
  virtual llvm::Type *llvm(llvm::LLVMContext &) const { UNREACHABLE(); }
#endif  // ICARUS_USE_LLVM

  base::vector<Type const *> entries_;

  mutable ir::Func *init_func_ = nullptr, *assign_func_ = nullptr,
                   *destroy_func_ = nullptr, *repr_func_ = nullptr;
};

Type const *Tup(base::vector<Type const *> entries);

}  // namespace type

#endif  // ICARUS_TYPE_TUPLE_H
