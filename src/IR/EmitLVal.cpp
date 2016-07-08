#include "IR.h"
#include "Type/Type.h"

namespace AST {
IR::Value Identifier::EmitLVal() { return decl->stack_loc; }

IR::Value Binop::EmitLVal() {
  if (op == Language::Operator::Index && lhs->type->is_array()) {
    auto array_type = (Array *)lhs->type;

    return IR::Access(array_type->data_type, rhs->EmitIR(),
                      array_type->fixed_length
                          ? lhs->EmitLVal()
                          : IR::ArrayData(array_type, lhs->EmitLVal()));
  } else {
    NOT_YET;
  }
}

IR::Value Unop::EmitLVal() {
  // TODO EmitIR or EmitLVal?
  if (op == Language::Operator::At) { return operand->EmitIR(); }
  NOT_YET;
}
IR::Value ChainOp::EmitLVal() { NOT_YET; }
IR::Value DummyTypeExpr::EmitLVal() { NOT_YET; }
IR::Value Generic::EmitLVal() { NOT_YET; }
IR::Value InDecl::EmitLVal() { NOT_YET; }

IR::Value Access::EmitLVal() { 
  // Automatically pass through pointers
  auto etype  = operand->type;
  auto e_lval = operand->EmitLVal();

  while (etype->is_pointer()) {
    e_lval = IR::Load(etype, e_lval);
    etype  = ((Pointer *)etype)->pointee;
  }

  assert(etype->is_struct());
  auto struct_type = (Structure *)etype;
  return IR::Field(struct_type, e_lval,
                   struct_type->field_name_to_num AT(member_name));
}

IR::Value Terminal::EmitLVal() { UNREACHABLE; }
IR::Value Declaration::EmitLVal() { UNREACHABLE; }
IR::Value FunctionLiteral::EmitLVal() { UNREACHABLE; }
IR::Value ArrayType::EmitLVal() { UNREACHABLE; }
IR::Value StructLiteral::EmitLVal() { UNREACHABLE; }
IR::Value ParametricStructLiteral::EmitLVal() { UNREACHABLE; }
IR::Value Case::EmitLVal() { UNREACHABLE; }
IR::Value ArrayLiteral::EmitLVal() { UNREACHABLE; }
IR::Value EnumLiteral::EmitLVal() { UNREACHABLE; }
} // namespace AST
