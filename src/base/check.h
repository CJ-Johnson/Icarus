#ifndef ICARUS_BASE_CHECK_H
#define ICARUS_BASE_CHECK_H

#include <iostream>
#include "base/string.h"

namespace base::check {
namespace internal {
struct LhsStealer {
  using line_t = decltype(__LINE__);
  LhsStealer(const char *f, line_t l, const char *e)
      : file(f), line(l), expr(e) {}
  LhsStealer(const LhsStealer &) = default;
  const char *file;
  line_t line;
  const char *expr;
};

template <typename T>
struct Expr {
  Expr(LhsStealer s, T &&v) : stealer(s), val(std::forward<T>(v)) {}
  LhsStealer stealer;
  T val;
};

template <>
struct Expr<bool> {
  Expr(LhsStealer s, bool b) : stealer(s), val(b) {}

  LhsStealer stealer;
  bool val;
  operator bool() {
    if (val) { return true; }
    LOG << "Expectation failed (" << stealer.file << ", line #" << stealer.line
        << ")\n"
        << stealer.expr << "\n(Value was false but expected to be true).\n\n";
    return false;
  }
};

template <>
struct Expr<bool &> : public Expr<bool> {
  Expr(LhsStealer s, bool b) : Expr<bool>(s, b) {}
};

template <typename T>
Expr<T> operator<<(LhsStealer stealer, T &&val) {
  return Expr<T>(stealer, std::forward<T>(val));
}

template <typename T, typename U>
auto operator<<(const Expr<T> &lhs, U &&rhs) {
  return Expr<decltype(std::declval<T>() << std::declval<U>())>(
      lhs.stealer, std::forward<T>(lhs.val) << std::forward<U>(rhs));
}

template <typename T, typename U>
auto operator>>(const Expr<T> &lhs, U &&rhs) {
  return Expr<decltype(std::declval<T>() >> std::declval<U>())>(
      lhs.stealer, std::forward<T>(lhs.val) >> std::forward<U>(rhs));
}

#define MAKE_OPERATOR(op)                                                      \
  template <typename T, typename U>                                            \
  inline bool operator op(const Expr<T> &lhs, U &&rhs) {                       \
    if (lhs.val op rhs) { return true; }                                       \
    LOG << "Expectation failed (" << lhs.stealer.file << ", line #"            \
        << lhs.stealer.line << ")\n\n  " << lhs.stealer.expr                   \
        << "\n\nleft-hand side:  " << base::internal::stringify(lhs.val)       \
        << "\nright-hand side: " << base::internal::stringify(rhs) << "\n\n";  \
    return false;                                                              \
  }
MAKE_OPERATOR(<)
MAKE_OPERATOR(<=)
MAKE_OPERATOR(==)
MAKE_OPERATOR(!=)
MAKE_OPERATOR(>=)
MAKE_OPERATOR(>)
#undef MAKE_OPERATOR

template <typename T, typename M>
bool operator,(Expr<T> &&e, M &&m) {
  auto str    = base::internal::stringify(e.val);
  auto result = std::forward<M>(m)(std::move(e.val));
  if (result.empty()) { return true; }
  std::cerr << "Expectation failed (" << e.stealer.file << ", line #"
            << e.stealer.line << ")\n\n  " << e.stealer.expr
            << "\n\nFound:    " << str << "\nExpected: " << result << "\n\n";
  return false;
}
}  // namespace internal

template <typename T>
auto Is() {
  return [](const auto &v) -> std::string {
    return v->template is<T>() ? "" : "Unexpected type";
  };
}
}  // namespace base::check

#endif  // ICARUS_BASE_CHECK_H