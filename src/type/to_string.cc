#include "type/all.h"

#include <cstring>

namespace type {

size_t Primitive::string_size() const {
  switch (type_) {
#define PRIMITIVE_MACRO(EnumName, name)                                        \
  case PrimType::EnumName:                                                     \
    return sizeof(name) - 1;
#include "type/primitive.xmacro.h"
#undef PRIMITIVE_MACRO
    default: UNREACHABLE();
  }
}

char *Primitive::WriteTo(char *buf) const {
  switch (type_) {
#define PRIMITIVE_MACRO(EnumName, name)                                        \
  case PrimType::EnumName:                                                     \
    return std::strcpy(buf, name) + string_size();
#include "type/primitive.xmacro.h"
#undef PRIMITIVE_MACRO
    default: UNREACHABLE();
  }
}

static size_t NumDigits(size_t num) {
  size_t num_digits = 1;
  while (num >= 10) {
    num /= 10;
    num_digits++;
  }
  return num_digits;
}

size_t Array::string_size() const {
  size_t result = 1 + NumDigits(len);

  Type const *const *type_ptr_ptr = &data_type;
  while ((*type_ptr_ptr)->is<Array>()) {
    auto array_ptr = &(*type_ptr_ptr)->as<const Array>();
    result += 2 + NumDigits(array_ptr->len);
    type_ptr_ptr = &array_ptr->data_type;
  }
  return result + 3 + (*type_ptr_ptr)->string_size();
}

char *Array::WriteTo(char *buf) const {
  buf = std::strcpy(buf, "[") + 1;
  buf = std::strcpy(buf, std::to_string(len).c_str()) + NumDigits(len);

  Type const *const *type_ptr_ptr = &data_type;
  while ((*type_ptr_ptr)->is<Array>()) {
    auto array_ptr = &(*type_ptr_ptr)->as<const Array>();

    buf = std::strcpy(buf, ", ") + 2;
    buf = std::strcpy(buf, std::to_string(array_ptr->len).c_str()) +
          NumDigits(array_ptr->len);
    type_ptr_ptr = &array_ptr->data_type;
  }
  buf = std::strcpy(buf, "; ") + 2;
  buf = (*type_ptr_ptr)->WriteTo(buf);
  buf = std::strcpy(buf, "]") + 1;
  return buf;
}

size_t Struct::string_size() const {
  return 7 + std::to_string(reinterpret_cast<uintptr_t>(this)).size();
}
char *Struct::WriteTo(char *buf) const {
  buf = std::strcpy(buf, "struct.") + 7;
  auto str = std::to_string(reinterpret_cast<uintptr_t>(this));
  return std::strcpy(buf, str.c_str()) + str.size();
}

size_t GenericStruct::string_size() const {
  if (deps_.empty()) { return 10; }
  size_t result = 2 * deps_.size() + 8;
  for (Type const *t : deps_) { result += t->string_size(); }
  return result;
}

char *GenericStruct::WriteTo(char *buf) const {
  auto x = buf;
  if (deps_.empty()) {
    buf = std::strcpy(buf, "[; struct]") + 10;
    return buf;
  }
  buf = std::strcpy(buf, "[") + 1;
  buf = deps_[0]->WriteTo(buf);
  for (size_t i = 1; i < deps_.size(); ++i) {
    buf = std::strcpy(buf, ", ") + 2;
    buf = deps_[i]->WriteTo(buf);
  }
  buf = std::strcpy(buf, "; struct]") + 9;
  return buf;
}

size_t Enum::string_size() const {
  return 5 + std::to_string(reinterpret_cast<uintptr_t>(this)).size();
}

char *Enum::WriteTo(char *buf) const {
  buf      = std::strcpy(buf, "enum.") + 5;
  auto str = std::to_string(reinterpret_cast<uintptr_t>(this));
  return std::strcpy(buf, str.c_str()) + str.size();
}

size_t Flags::string_size() const {
  return 6 + std::to_string(reinterpret_cast<uintptr_t>(this)).size();
}
char *Flags::WriteTo(char *buf) const {
  buf = std::strcpy(buf, "flags.") + 6;
  auto str = std::to_string(reinterpret_cast<uintptr_t>(this));
  return std::strcpy(buf, str.c_str()) + str.size();
}

size_t BufferPointer::string_size() const {
  return ((pointee->is<Struct>() || pointee->is<Primitive>() ||
           pointee->is<Enum>() || pointee->is<Flags>() ||
           pointee->is<Array>() || pointee->is<Pointer>())
              ? 3
              : 5) +
         pointee->string_size();
}

size_t Pointer::string_size() const {
  return ((pointee->is<Struct>() || pointee->is<Primitive>() ||
           pointee->is<Enum>() || pointee->is<Flags>() ||
           pointee->is<Array>() || pointee->is<Pointer>())
              ? 1
              : 3) +
         pointee->string_size();
}

char *BufferPointer::WriteTo(char *buf) const {
  if (pointee->is<Struct>() || pointee->is<Primitive>() ||
      pointee->is<Enum>() || pointee->is<Flags>() || pointee->is<Array>() ||
      pointee->is<Pointer>()) {
    buf = std::strcpy(buf, "[*]") + 3;
    buf = pointee->WriteTo(buf);
  } else {
    buf = std::strcpy(buf, "[*](") + 4;
    buf = pointee->WriteTo(buf);
    buf = std::strcpy(buf, ")") + 1;
  }
  return buf;
}

char *Pointer::WriteTo(char *buf) const {
  if (pointee->is<Struct>() || pointee->is<Primitive>() ||
      pointee->is<Enum>() || pointee->is<Flags>() || pointee->is<Array>() ||
      pointee->is<Pointer>()) {
    buf = std::strcpy(buf, "*") + 1;
    buf = pointee->WriteTo(buf);
  } else {
    buf = std::strcpy(buf, "*(") + 2;
    buf = pointee->WriteTo(buf);
    buf = std::strcpy(buf, ")") + 1;
  }
  return buf;
}
size_t Function::string_size() const {
  size_t acc = 0;
  for (Type const *t : input) { acc += t->string_size(); }
  for (Type const *t : output) { acc += t->string_size(); }
  acc += 2 * (input.size() - 1) +        // space between inputs
         (input.size() == 1 ? 0 : 2) +   // Parens
         (input.empty() ? 4 : 0) +       // void
         4 +                             // " -> "
         2 * (output.size() - 1) +       // space between outputs
         (output.size() == 1 ? 0 : 2) +  // Parens
         (output.empty() ? 4 : 0) +      // void
         (input.size() == 1 && input[0]->is<Function>() ? 2 : 0);  // parens
  return acc;
}

char *Function::WriteTo(char *buf) const {
  if (input.empty()) {
    buf = std::strcpy(buf, "void") + 4;
  } else if (input.size() == 1 && !input[0]->is<Function>()) {
    buf = input[0]->WriteTo(buf);
  } else {
    buf = std::strcpy(buf, "(") + 1;
    buf = input[0]->WriteTo(buf);
    for (size_t i = 1; i < input.size(); ++i) {
      buf = std::strcpy(buf, ", ") + 2;
      buf = input[i]->WriteTo(buf);
    }
    buf = std::strcpy(buf, ")") + 1;
  }

  buf = std::strcpy(buf, " -> ") + 4;

  if (output.empty()) {
    buf = std::strcpy(buf, "void") + 4;
  } else if (output.size() == 1) {
    buf = output[0]->WriteTo(buf);
  } else {
    buf = std::strcpy(buf, "(") + 1;
    buf = output[0]->WriteTo(buf);
    for (size_t i = 1; i < output.size(); ++i) {
      buf = std::strcpy(buf, ", ") + 2;
      buf = output[i]->WriteTo(buf);
    }
    buf = std::strcpy(buf, ")") + 1;
  }

  return buf;
}

size_t Variant::string_size() const {
  size_t result = (variants_.size() - 1) * 3;
  for (Type const *v : variants_) {
    result += v->string_size() + (v->is<Struct>() || v->is<Primitive>() ||
                                          v->is<Enum>() || v->is<Flags>() ||
                                          v->is<Pointer>() ||
                                          v->is<Function>() || v->is<Array>()
                                      ? 0
                                      : 2);
  }
  return result;
}

char *Variant::WriteTo(char *buf) const {
  auto iter = variants_.begin();

  if ((*iter)->is<Struct>() || (*iter)->is<Primitive>() ||
      (*iter)->is<Enum>() || (*iter)->is<Flags>() || (*iter)->is<Pointer>() ||
      (*iter)->is<Function>() || (*iter)->is<Array>()) {
    buf = (*iter)->WriteTo(buf);
  } else {
    buf = std::strcpy(buf, "(") + 1;
    buf = (*iter)->WriteTo(buf);
    buf = std::strcpy(buf, ")") + 1;
  }

  ++iter;
  for (; iter != variants_.end(); ++iter) {
    buf = std::strcpy(buf, " | ") + 3;
    if ((*iter)->is<Struct>() || (*iter)->is<Primitive>() ||
        (*iter)->is<Enum>() || (*iter)->is<Flags>() || (*iter)->is<Pointer>() ||
        (*iter)->is<Function>() || (*iter)->is<Array>()) {
      buf = (*iter)->WriteTo(buf);
    } else {
      buf = std::strcpy(buf, "(") + 1;
      buf = (*iter)->WriteTo(buf);
      buf = std::strcpy(buf, ")") + 1;
    }
  }
  return buf;
}

size_t Tuple::string_size() const {
  size_t result = std::max<size_t>(2, 2 * entries_.size());
  for (Type const *t : entries_) { result += t->string_size(); }
  return result;
}

char *Tuple::WriteTo(char *buf) const {
  if (entries_.empty()) {
    buf = std::strcpy(buf, "()") + 2;
    return buf;
  }
  buf       = std::strcpy(buf, "(") + 1;
  auto iter = entries_.begin();
  buf       = (*iter)->WriteTo(buf);
  ++iter;
  for (; iter != entries_.end(); ++iter) {
    buf = std::strcpy(buf, ", ") + 2;
    buf = (*iter)->WriteTo(buf);
  }
  buf = std::strcpy(buf, ")") + 1;
  return buf;
}

char *Opaque::WriteTo(char *buf) const {
  return std::strcpy(buf, "<opaque>") + 8;
}
size_t Opaque::string_size() const { return 8; }

}  // namespace type
