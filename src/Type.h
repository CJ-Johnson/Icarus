#ifndef ICARUS_TYPE_H
#define ICARUS_TYPE_H

#include <vector>
#include <string>
#include <map>

// TODO Figure out what you need from this.
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

// TODO why is replace(_, _) not const?

namespace AST {
  class Expression;
  class Declaration;
}  // namespace AST

class Function;
class Pointer;

class Type {
  public:
    enum time_loc {
      either_time  = 0x0,
      compile_time = 0x1,
      run_time     = 0x2,
      mixed_time   = 0x3
    };

    static Type* get_type_error();
    static Type* get_unknown();
    static Type* get_bool();
    static Type* get_char();
    static Type* get_int();
    static Type* get_real();
    static Type* get_type();
    static Type* get_uint();
    static Type* get_void();

    static Function* get_function(Type* in, Type* out);
    static Type* get_pointer(Type* t);
    static Type* get_tuple(const std::vector<Type*>& types);
    static Type* get_array(Type* t, int len = -1);

    static std::map<std::string, Type*> literals;

    virtual llvm::Value* allocate(llvm::IRBuilder<>& bldr) const = 0;
    virtual size_t bytes() const = 0;
    virtual void initialize(llvm::IRBuilder<>& bldr, llvm::Value* var) const = 0;
    virtual llvm::Function* print_function() = 0;
    virtual Type* replace(Type* pattern, Type* replacement) = 0;
    virtual std::string to_string() const = 0;
    virtual Type::time_loc type_time() const = 0;

    virtual bool is_array()     const { return false; }
    virtual bool is_function()  const { return false; }
    virtual bool is_pointer()   const { return false; }
    virtual bool is_primitive() const { return false; }
    virtual bool is_tuple()     const { return false; }
    virtual bool is_void()      const { return this == Type::get_void(); }


    llvm::Type* llvm() const { return llvm_type_; }

    Type() : print_fn_(nullptr) {}
    virtual ~Type() {}

  protected:
    llvm::Function* print_fn_;
    llvm::Type* llvm_type_;
};

class Primitive : public Type {
  public:
    static constexpr size_t num_primitive_types_ = 9;

    friend class Type;
    virtual ~Primitive() {}

    virtual llvm::Value* allocate(llvm::IRBuilder<>& bldr) const;
    virtual size_t bytes() const;
    virtual void initialize(llvm::IRBuilder<>& bldr, llvm::Value* var) const;
    virtual llvm::Function* print_function();
    virtual Type* replace(Type* pattern, Type* replacement);
    virtual std::string to_string() const;
    virtual Type::time_loc type_time() const;

    virtual bool is_primitive() const { return true; }

  private:
    // This needs to be an enum becasue we use it to access other arrays and
    // vectors
    enum PrimitiveEnum {
      t_type_error, t_unknown, t_bool, t_char, t_int, t_real, t_type, t_uint, t_void
    };


    PrimitiveEnum prim_type_;

    Primitive(PrimitiveEnum pe);

    static Primitive primitive_types_[ num_primitive_types_ ];
    static llvm::Type* llvm_types_[ num_primitive_types_ ];
};

class Function : public Type {
  public:
    friend class Type;
    virtual bool is_function() const { return true; }
    Type* argument_type() const { return input_type_; }
    Type* return_type() const { return output_type_; }

    llvm::FunctionType* llvm() const {
      return static_cast<llvm::FunctionType*>(llvm_type_);
    }

    virtual llvm::Value* allocate(llvm::IRBuilder<>& bldr) const;
    virtual size_t bytes() const;
    virtual void initialize(llvm::IRBuilder<>& bldr, llvm::Value* var) const;
    virtual llvm::Function* print_function();
    virtual Type* replace(Type* pattern, Type* replacement);
    virtual std::string to_string() const;
    virtual Type::time_loc type_time() const;

    virtual ~Function() {}

  private:
    Function(Type* in, Type* out) : input_type_(in), output_type_(out) {
      // TODO should I expand an input tuple into a vector?
      auto llvm_input = input_type_->is_function()
        ? Type::get_pointer(input_type_)->llvm()
        : input_type_->llvm();

      auto llvm_output = output_type_->is_function()
        ? Type::get_pointer(output_type_)->llvm()
        : output_type_->llvm();

      std::vector<llvm::Type*> input_list(1, llvm_input);

      if (input_type_ == get_void()) {
        input_list.clear();
      }

      llvm_type_ = llvm::FunctionType::get( llvm_output,
          input_list, false);
    }

    Type* input_type_;
    Type* output_type_;

    static std::vector<Function*> fn_types_;
};

class Pointer : public Type {
  public:
    friend class Type;

    virtual bool is_pointer() const { return true; }

    virtual llvm::Value* allocate(llvm::IRBuilder<>& bldr) const;
    virtual size_t bytes() const;
    virtual void initialize(llvm::IRBuilder<>& bldr, llvm::Value* var) const;
    virtual llvm::Function* print_function();
    virtual Type* replace(Type* pattern, Type* replacement);
    virtual std::string to_string() const;
    virtual Type::time_loc type_time() const;

    virtual ~Pointer() {}

  private:
    Pointer(Type* t) : pointee_type_(t) {
      llvm_type_ = llvm::PointerType::get(t->llvm(), 0);
    }
    Type* pointee_type_;

    static std::vector<Pointer*> pointer_types_;
};


class Tuple : public Type {
  public:
    friend class Type;

    virtual bool is_tuple() const { return true; }

    virtual llvm::Value* allocate(llvm::IRBuilder<>& bldr) const;
    virtual size_t bytes() const;
    virtual void initialize(llvm::IRBuilder<>& bldr, llvm::Value* var) const;
    virtual llvm::Function* print_function();
    virtual Type* replace(Type* pattern, Type* replacement);
    virtual std::string to_string() const;
    virtual Type::time_loc type_time() const;

    virtual ~Tuple() {}

  private:
    Tuple(const std::vector<Type*>& types) : entry_types_(types) {}

    std::vector<Type*> entry_types_;

    static std::vector<Tuple*> tuple_types_;
};

class Array : public Type {
  public:
    friend class AST::Declaration;
    friend class Type;

    virtual bool is_array() const { return true; }

    virtual llvm::Value* allocate(llvm::IRBuilder<>& bldr) const;
    virtual size_t bytes() const;
    virtual void initialize(llvm::IRBuilder<>& bldr, llvm::Value* var) const;
    virtual llvm::Function* print_function();
    virtual Type* replace(Type* pattern, Type* replacement);
    virtual std::string to_string() const;
    virtual Type::time_loc type_time() const;

    virtual Type* data_type() const { return type_; }
    virtual bool has_dynamic_length() const { return len_ == -1; }
    virtual llvm::Value* initialize_literal(llvm::IRBuilder<>& bldr,
        llvm::Value* runtime_len = nullptr) const;

    virtual ~Array() {}

  private:
    // A value of -1 for the length means this is to be dependently typed. All
    // other values are the actual type
    Array(Type* t, int len = -1) : type_(t), len_(len) {
//      if (has_dynamic_length()) {
      llvm_type_ = llvm::PointerType::getUnqual(t->llvm());
//      } else {
//        llvm_type_ = llvm::ArrayType::get(t->llvm(), static_cast<size_t>(len));
//      }
    }

    Type* type_;
    int len_;

    static std::vector<Array*> array_types_;
};

#endif
