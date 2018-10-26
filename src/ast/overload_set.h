#ifndef ICARUS_AST_OVERLOAD_SET_H
#define ICARUS_AST_OVERLOAD_SET_H

#include <string>

#include "base/container/vector.h"
#include "type/typed_value.h"
#include "type/function.h"

struct Scope;
struct Context;

namespace AST {
struct Expression;

struct OverloadSet
    : public base::vector<type::Typed<Expression *, type::Function>> {
  OverloadSet() = default;
  OverloadSet(Scope *scope, std::string const &id, Context *ctx);
};
}  // namespace AST

#endif  // ICARUS_AST_OVERLOAD_SET_H