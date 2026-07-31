#pragma once

#include <string>
#include <utility>
#include <vector>

// Minimal self-contained JSON value type, parser, and writer.
//
// Scope is deliberately small: it covers exactly what the scene serializer
// needs (objects, arrays, numbers, strings, booleans, null) while keeping the
// compile-time and code-size footprint near zero. No exceptions are thrown;
// every failure is reported through an optional error string.
//
// Object members preserve insertion order, so a serialized scene reads back in
// the exact order it was written.

namespace json {

enum class Type
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

class Value
{
public:
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    // --- construction helpers ---
    static Value MakeNull()      { return Value(); }
    static Value MakeBool(bool v) { Value r; r.type = Type::Bool;   r.b = v; return r; }
    static Value MakeNumber(double v) { Value r; r.type = Type::Number; r.num = v; return r; }
    static Value MakeString(const std::string &v) { Value r; r.type = Type::String; r.str = v; return r; }
    static Value MakeArray()     { Value r; r.type = Type::Array;   return r; }
    static Value MakeObject()    { Value r; r.type = Type::Object;  return r; }

    // --- type queries ---
    bool IsNull()   const { return type == Type::Null; }
    bool IsBool()   const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray()  const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // --- object member access (returns nullptr when absent / not an object) ---
    const Value *Find(const std::string &key) const;
    Value *Find(const std::string &key);

    // Typed lookups with defaults; safe on any Value.
    double      Number(const std::string &key, double def = 0.0) const;
    std::string String(const std::string &key, const std::string &def = "") const;
    bool        Bool(const std::string &key, bool def = false) const;

    // --- array helpers ---
    size_t Size() const;
    const Value &At(size_t index) const;
};

// Parse `text` into a Value. On failure returns a Null value and, when `error`
// is non-null, describes the problem.
Value Parse(const std::string &text, std::string *error = nullptr);

// Write a Value back out. `Write` is compact; `WritePretty` indents with
// `indent` spaces (2 by default) for human-readable scene files.
std::string Write(const Value &value);
std::string WritePretty(const Value &value, int indent = 2);

} // namespace json
