#ifndef ICARUS_IR_REGISTER_H
#define ICARUS_IR_REGISTER_H

#include "base/container/vector.h"
#include "base/strong_types.h"
#include "base/types.h"

DEFINE_STRONG_INT(ir, BlockIndex, i32, -1);
DEFINE_STRONG_INT(ir, EnumVal, size_t, 0);
DEFINE_STRONG_INT(ir, BuiltinGenericIndex, i32, -1);

DEFINE_STRONG_INT(ir, Register, i32, std::numeric_limits<i32>::lowest());

namespace ir {
#define MAKE_CMP(type)                                                         \
  inline bool operator<(type lhs, type rhs) { return lhs.value < rhs.value; }  \
  inline bool operator<=(type lhs, type rhs) { return !(rhs < lhs); }          \
  inline bool operator>(type lhs, type rhs) { return rhs < lhs; }              \
  inline bool operator>=(type lhs, type rhs) { return !(lhs < rhs); }
MAKE_CMP(BlockIndex)
MAKE_CMP(EnumVal)
MAKE_CMP(Register)
MAKE_CMP(BuiltinGenericIndex)
#undef MAKE_CMP
}  // namespace ir

namespace ast {
struct Expression;
struct BlockLiteral;
}  // namespace ast

namespace ir {
struct Func;

struct BlockSequence {
  base::vector<ast::BlockLiteral *> const *seq_;
};
inline std::ostream &operator<<(std::ostream &os, BlockSequence b) {
  return os << base::internal::stringify(*b.seq_);
}

// TODO not really comparable. just for variant? :(
inline bool operator==(const BlockSequence &lhs, const BlockSequence &rhs) {
  return lhs.seq_ == rhs.seq_;
}

// TODO not really comparable. just for variant? :(
inline bool operator<(const BlockSequence &lhs, const BlockSequence &rhs) {
  return lhs.seq_ < rhs.seq_;
}

inline std::ostream &operator<<(std::ostream &os, Register r) {
  return os << "reg." << r.value;
}

inline std::ostream &operator<<(std::ostream &os, EnumVal e) {
  return os << e.value;
}

inline std::ostream &operator<<(std::ostream &os, BlockIndex b) {
  return os << "block." << b.value;
}

template <typename T>
struct RegisterOr {
  using type = T;
  static_assert(!std::is_same_v<Register, T>);
  RegisterOr() : reg_(-1), is_reg_(true) {}

  RegisterOr(Register reg) : reg_(reg), is_reg_(true) {}
  RegisterOr(T val) : val_(val), is_reg_(false) {}

  union {
    Register reg_;
    T val_;
  };
  bool is_reg_;

  inline friend std::ostream &operator<<(std::ostream &os,
                                         RegisterOr const &r) {
    if (r.is_reg_) {
      return os << r.reg_;
    } else {
      return os << r.val_;
    }
  }
};

template <typename T>
struct TypedRegister : public Register {
  using type = T;
  TypedRegister(Register r) : Register(r) {}
  operator RegisterOr<T>() const { return static_cast<Register>(*this); }
};

template <typename T>
struct IsRegOr : public std::false_type {};
template <typename T>
struct IsRegOr<RegisterOr<T>> : public std::true_type {};

template <typename T>
struct IsTypedReg : public std::false_type {};
template <typename T>
struct IsTypedReg<TypedRegister<T>> : public std::true_type {};

}  // namespace ir

#endif  // ICARUS_IR_REGISTER_H
