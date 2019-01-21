#ifndef ICARUS_AST_FN_PARAMS_H
#define ICARUS_AST_FN_PARAMS_H

#include "base/container/vector.h"
#include "base/container/unordered_map.h"

#include <string_view>

namespace ast {
template <typename T>
struct FnParams {
  struct Param {
    Param(std::string_view s, T t) : name(s), value(std::move(t)) {}
    Param(Param&&) noexcept = default;
    Param& operator=(Param&&) noexcept = default;

    Param(Param const&) noexcept = default;
    Param& operator=(Param const&) noexcept = default;

    std::string_view name;
    T value;
  };

  size_t size() const { return params_.size(); }
  bool empty() const { return params_.empty(); }
  void reserve(size_t n) { params_.reserve(n); }

  Param const& at(size_t i) const & { return params_.at(i); }
  Param& at(size_t i) & { return params_.at(i); }

  void append(std::string_view name, T val) {
    lookup_.emplace(name, params_.size());
    params_.emplace_back(name, std::move(val));
  }

  base::vector<Param> params_;

  // Maps the string name of the declared argument to it's index:
  // Example: (a: int, b: char, c: string) -> int
  //           a => 0, b => 1, c => 2
  base::unordered_map<std::string_view, size_t> lookup_;
};
}  // namespace ast

#endif  // ICARUS_AST_FN_PARAMS_H
