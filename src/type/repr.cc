#include "type.h"
#include "../scope.h"

extern IR::Value PtrCallFix(Type *t, IR::Value v);

static void AddSpecialCharacter(IR::Value val, char c, char vis,
                                IR::Block *land) {
  auto eq            = IR::EQ(Char, val, IR::Value::Char(c));
  auto special_block = IR::Func::Current->AddBlock("special");
  auto next_block    = IR::Func::Current->AddBlock("next");

  IR::Block::Current->SetConditional(eq, special_block, next_block);
  IR::Block::Current = special_block;
  IR::Print(IR::Value::Type(Char), IR::Value::Char('\\'));
  IR::Print(IR::Value::Type(Char), IR::Value::Char(vis));
  IR::Block::Current->SetUnconditional(land);
  IR::Block::Current = next_block;
}

void Primitive::EmitRepr(IR::Value val) {
  if (this == Bool) {
    IR::Print(IR::Value::Type(Bool), val);
  } else if (this == Char) {
    if (!repr_func) {
      auto saved_func  = IR::Func::Current;
      auto saved_block = IR::Block::Current;

      repr_func = new IR::Func(Func(this, Void));
      implicit_functions.push_back(repr_func);
      IR::Func::Current  = repr_func;
      IR::Block::Current = repr_func->entry();
      repr_func->SetName("repr." + Mangle(this));

      IR::Print(IR::Value::Type(Char), IR::Value::Char('`'));

      auto land_block = IR::Func::Current->AddBlock("land");
      AddSpecialCharacter(IR::Value::Arg(0), '\a', 'a', land_block);
      AddSpecialCharacter(IR::Value::Arg(0), '\b', 'b', land_block);
      AddSpecialCharacter(IR::Value::Arg(0), '\n', 'n', land_block);
      AddSpecialCharacter(IR::Value::Arg(0), '\r', 'r', land_block);
      AddSpecialCharacter(IR::Value::Arg(0), '\t', 't', land_block);
      AddSpecialCharacter(IR::Value::Arg(0), '\v', 'v', land_block);

      IR::Print(IR::Value::Type(Char), IR::Value::Arg(0));
      IR::Block::Current->SetUnconditional(land_block);
      IR::Block::Current = land_block;

      IR::Block::Current->SetUnconditional(IR::Func::Current->exit());
      IR::Block::Current = IR::Func::Current->exit();
      IR::Block::Current->SetReturnVoid();

      IR::Func::Current  = saved_func;
      IR::Block::Current = saved_block;
    }
    assert(repr_func);

    IR::Call(Void, IR::Value::Func(repr_func), {val});
  } else if (this == Int) {
    IR::Print(IR::Value::Type(Int), val);

  } else if (this == Uint) {
    IR::Print(IR::Value::Type(Uint), val);
    IR::Print(IR::Value::Type(Char), IR::Value::Char('u'));

  } else if (this == Real) {
    IR::Print(IR::Value::Type(Real), val);

  } else if (this == Type_) {
    NOT_YET;

  } else {
    NOT_YET;
  }
}

void Function::EmitRepr(IR::Value) {
  IR::Print(IR::Value::Type(Char), IR::Value::Char('{'));
  IR::Print(IR::Value::Type(Type_), IR::Value::Type(this));
  IR::Print(IR::Value::Type(Char), IR::Value::Char('}'));
}

void Enum::EmitRepr(IR::Value val) { NOT_YET; }
void Pointer::EmitRepr(IR::Value val) { NOT_YET; }

void Array::EmitRepr(IR::Value val) {
  if (!repr_func) {
    auto saved_func  = IR::Func::Current;
    auto saved_block = IR::Block::Current;

    repr_func          = new IR::Func(Func(this, Void));
    repr_func->SetName("repr." + Mangle(this));

    implicit_functions.push_back(repr_func);
    IR::Func::Current  = repr_func;
    IR::Block::Current = repr_func->entry();

    IR::Print(IR::Value::Type(Char), IR::Value::Char('['));
    // TODO length == 1 special case (don't print extra ,)
    IR::Value ptr, length_var;
    if (fixed_length) {
      ptr        = IR::Access(data_type, IR::Value::Uint(0), IR::Value::Arg(0));
      length_var = IR::Value::Uint(len);
    } else {
      ptr        = IR::Load(Ptr(data_type), IR::ArrayData(this, IR::Value::Arg(0)));
      length_var = IR::Load(Uint, IR::ArrayLength(IR::Value::Arg(0)));
    }

    auto init_block = IR::Func::Current->AddBlock("loop-init");
    auto land       = IR::Func::Current->AddBlock("land");

    IR::Block::Current->SetConditional(
        IR::EQ(Uint,length_var, IR::Value::Uint(0ul)), land, init_block);

    IR::Block::Current = init_block;
    data_type->EmitRepr(PtrCallFix(data_type, ptr));

    auto end_ptr = IR::PtrIncr(Ptr(data_type), ptr, length_var);

    auto loop_phi  = IR::Func::Current->AddBlock("loop-phi");
    auto loop_cond = IR::Func::Current->AddBlock("loop-cond");
    auto loop_body = IR::Func::Current->AddBlock("loop-body");

    IR::Block::Current->SetUnconditional(loop_phi);
    IR::Block::Current = loop_phi;

    auto phi = IR::Phi(Ptr(data_type));
    phi.args.emplace_back(init_block);
    phi.args.emplace_back(ptr);

    auto phi_reg = IR::Value::Reg(phi.result.reg);

    loop_phi->SetUnconditional(loop_cond);
    IR::Block::Current = loop_cond;

    auto elem_ptr = IR::Increment(Ptr(data_type), phi_reg);
    auto cond = IR::EQ(Ptr(data_type), elem_ptr, end_ptr);
    IR::Block::Current->SetConditional(cond, land, loop_body);
    IR::Block::Current = loop_body;

    IR::Print(IR::Value::Type(Char), IR::Value::Char(','));
    IR::Print(IR::Value::Type(Char), IR::Value::Char(' '));
    data_type->EmitRepr(PtrCallFix(data_type, elem_ptr));

    phi.args.emplace_back(IR::Block::Current);
    phi.args.emplace_back(elem_ptr);

    IR::Block::Current->SetUnconditional(loop_phi);
    loop_phi->push(phi);

    IR::Block::Current = land;

    IR::Print(IR::Value::Type(Char), IR::Value::Char(']'));

    IR::Block::Current->SetUnconditional(IR::Func::Current->exit());
    IR::Block::Current = IR::Func::Current->exit();
    IR::Block::Current->SetReturnVoid();

    IR::Func::Current  = saved_func;
    IR::Block::Current = saved_block;
  }
  assert(repr_func);

  IR::Call(Void, IR::Value::Func(repr_func), {val});
}

void Tuple::EmitRepr(IR::Value val) { NOT_YET; }
void Struct::EmitRepr(IR::Value val) { NOT_YET; }
void TypeVariable::EmitRepr(IR::Value val) { NOT_YET; }
void RangeType::EmitRepr(IR::Value val) { NOT_YET; }
void SliceType::EmitRepr(IR::Value val) { NOT_YET; }
void Scope_Type::EmitRepr(IR::Value id_val) { NOT_YET; }