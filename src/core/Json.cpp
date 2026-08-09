#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const Value *Value::Find(const std::string &key) const
{
    if (!IsObject())
        return nullptr;
    for (const auto &kv : object)
        if (kv.first == key)
            return &kv.second;
    return nullptr;
}

Value *Value::Find(const std::string &key)
{
    if (!IsObject())
        return nullptr;
    for (auto &kv : object)
        if (kv.first == key)
            return &kv.second;
    return nullptr;
}

double Value::Number(const std::string &key, double def) const
{
    const Value *v = Find(key);
    return (v && v->IsNumber()) ? v->num : def;
}

std::string Value::String(const std::string &key, const std::string &def) const
{
    const Value *v = Find(key);
    return (v && v->IsString()) ? v->str : def;
}

bool Value::Bool(const std::string &key, bool def) const
{
    const Value *v = Find(key);
    return (v && v->IsBool()) ? v->b : def;
}

size_t Value::Size() const
{
    switch (type)
    {
        case Type::Array:  return array.size();
        case Type::Object: return object.size();
        default:           return 0;
    }
}

const Value &Value::At(size_t index) const
{
    static const Value s_null;
    return (IsArray() && index < array.size()) ? array[index] : s_null;
}

// ---------------------------------------------------------------------------
// Parser (recursive descent over a std::string)
// ---------------------------------------------------------------------------

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Maximum value-nesting depth the parser will descend before rejecting the
// document. Each entity tree level costs a couple of nested value calls, so
// this comfortably covers every real scene/prefab while leaving the call
// stack far from exhaustion on hostile input.
static const int kMaxJsonDepth = 1024;

static void SkipWhitespace(const std::string &text, size_t &i)
{
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r'))
        ++i;
}

static bool Fail(std::string *error, const char *msg)
{
    if (error)
        *error = msg;
    return false;
}

static bool ParseValue(const std::string &text, size_t &i, Value &out, std::string *error, int depth);

static bool ParseString(const std::string &text, size_t &i, std::string &out, std::string *error)
{
    if (i >= text.size() || text[i] != '"')
        return Fail(error, "expected '\"'");
    ++i;
    out.clear();

    while (i < text.size())
    {
        char c = text[i];
        if (c == '"')
        {
            ++i;
            return true;
        }
        if (c == '\\')
        {
            ++i;
            if (i >= text.size())
                return Fail(error, "unterminated escape sequence");
            char e = text[i++];
            switch (e)
            {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    // \uXXXX with basic UTF-8 encoding (no surrogate pairs).
                    if (i + 4 > text.size())
                        return Fail(error, "short \\u escape");
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        char h = text[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')       cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f')  cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')  cp |= (unsigned)(h - 'A' + 10);
                        else
                            return Fail(error, "bad \\u hex digit");
                    }
                    i += 4;
                    if (cp < 0x80)
                        out.push_back((char)cp);
                    else if (cp < 0x800)
                    {
                        out.push_back((char)(0xC0 | (cp >> 6)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                    else
                    {
                        out.push_back((char)(0xE0 | (cp >> 12)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                } break;
                default:
                    return Fail(error, "unknown escape sequence");
            }
            continue;
        }
        if ((unsigned char)c < 0x20)
            return Fail(error, "unescaped control character in string");
        out.push_back(c);
        ++i;
    }
    return Fail(error, "unterminated string");
}

static bool ParseNumber(const std::string &text, size_t &i, Value &out, std::string *error)
{
    const size_t start = i;
    if (i < text.size() && text[i] == '-')
        ++i;

    if (i >= text.size() || !IsDigit(text[i]))
        return Fail(error, "malformed number");
    if (text[i] == '0')
        ++i;
    else
        while (i < text.size() && IsDigit(text[i]))
            ++i;

    if (i < text.size() && text[i] == '.')
    {
        ++i;
        if (i >= text.size() || !IsDigit(text[i]))
            return Fail(error, "malformed number fraction");
        while (i < text.size() && IsDigit(text[i]))
            ++i;
    }

    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
    {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            ++i;
        if (i >= text.size() || !IsDigit(text[i]))
            return Fail(error, "malformed number exponent");
        while (i < text.size() && IsDigit(text[i]))
            ++i;
    }

    std::string token = text.substr(start, i - start);
    out = Value::MakeNumber(std::strtod(token.c_str(), nullptr));
    return true;
}

static bool ParseArray(const std::string &text, size_t &i, Value &out, std::string *error, int depth)
{
    ++i; // '['
    Value arr = Value::MakeArray();
    SkipWhitespace(text, i);

    if (i < text.size() && text[i] == ']')
    {
        ++i;
        out = std::move(arr);
        return true;
    }

    while (i < text.size())
    {
        Value element;
        if (!ParseValue(text, i, element, error, depth + 1))
            return false;
        arr.array.push_back(std::move(element));

        SkipWhitespace(text, i);
        if (i < text.size() && text[i] == ',')
        {
            ++i;
            SkipWhitespace(text, i);
            continue;
        }
        if (i < text.size() && text[i] == ']')
        {
            ++i;
            out = std::move(arr);
            return true;
        }
        return Fail(error, "expected ',' or ']' in array");
    }
    return Fail(error, "unterminated array");
}

static bool ParseObject(const std::string &text, size_t &i, Value &out, std::string *error, int depth)
{
    ++i; // '{'
    Value obj = Value::MakeObject();
    SkipWhitespace(text, i);

    if (i < text.size() && text[i] == '}')
    {
        ++i;
        out = std::move(obj);
        return true;
    }

    while (i < text.size())
    {
        SkipWhitespace(text, i);
        if (i >= text.size() || text[i] != '"')
            return Fail(error, "expected object key string");
        std::string key;
        if (!ParseString(text, i, key, error))
            return false;

        SkipWhitespace(text, i);
        if (i >= text.size() || text[i] != ':')
            return Fail(error, "expected ':' after object key");
        ++i;

        Value value;
        if (!ParseValue(text, i, value, error, depth + 1))
            return false;
        obj.object.emplace_back(std::move(key), std::move(value));

        SkipWhitespace(text, i);
        if (i < text.size() && text[i] == ',')
        {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}')
        {
            ++i;
            out = std::move(obj);
            return true;
        }
        return Fail(error, "expected ',' or '}' in object");
    }
    return Fail(error, "unterminated object");
}

static bool ParseValue(const std::string &text, size_t &i, Value &out, std::string *error, int depth)
{
    // Recursive-descent guard: hostile / hand-edited documents nested deeper
    // than this are rejected cleanly instead of overflowing the call stack.
    if (depth > kMaxJsonDepth)
        return Fail(error, "nesting too deep");

    SkipWhitespace(text, i);
    if (i >= text.size())
        return Fail(error, "unexpected end of input");

    char c = text[i];
    switch (c)
    {
        case '{': return ParseObject(text, i, out, error, depth);
        case '[': return ParseArray(text, i, out, error, depth);
        case '"':
        {
            std::string s;
            if (!ParseString(text, i, s, error))
                return false;
            out = Value::MakeString(s);
            return true;
        }
        case 't':
            if (text.compare(i, 4, "true") == 0) { i += 4; out = Value::MakeBool(true); return true; }
            return Fail(error, "unexpected token");
        case 'f':
            if (text.compare(i, 5, "false") == 0) { i += 5; out = Value::MakeBool(false); return true; }
            return Fail(error, "unexpected token");
        case 'n':
            if (text.compare(i, 4, "null") == 0) { i += 4; out = Value::MakeNull(); return true; }
            return Fail(error, "unexpected token");
        default:
            if (c == '-' || IsDigit(c))
                return ParseNumber(text, i, out, error);
            return Fail(error, "unexpected character");
    }
}

Value Parse(const std::string &text, std::string *error)
{
    size_t i = 0;
    Value result;
    if (!ParseValue(text, i, result, error, 0))
        return Value();
    SkipWhitespace(text, i);
    if (i != text.size())
    {
        Fail(error, "trailing characters after JSON value");
        return Value();
    }
    return result;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

static std::string FormatNumber(double d)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.9g", d);
    return std::string(buf);
}

static void AppendEscaped(std::string &out, const std::string &s)
{
    out.push_back('"');
    for (char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                }
                else
                    out.push_back(c);
        }
    }
    out.push_back('"');
}

static void WriteNode(const Value &v, std::string &out, int depth, int indent, bool pretty);

static void WriteArray(const Value &v, std::string &out, int depth, int indent, bool pretty)
{
    if (v.array.empty())
    {
        out += "[]";
        return;
    }
    out += pretty ? "[\n" : "[";
    for (size_t k = 0; k < v.array.size(); ++k)
    {
        if (pretty)
            out.append((size_t)((depth + 1) * indent), ' ');
        WriteNode(v.array[k], out, depth + 1, indent, pretty);
        if (k + 1 < v.array.size())
            out.push_back(',');
        if (pretty)
            out.push_back('\n');
    }
    if (pretty)
        out.append((size_t)(depth * indent), ' ');
    out.push_back(']');
}

static void WriteObject(const Value &v, std::string &out, int depth, int indent, bool pretty)
{
    if (v.object.empty())
    {
        out += "{}";
        return;
    }
    out += pretty ? "{\n" : "{";
    for (size_t k = 0; k < v.object.size(); ++k)
    {
        if (pretty)
            out.append((size_t)((depth + 1) * indent), ' ');
        AppendEscaped(out, v.object[k].first);
        out += pretty ? ": " : ":";
        WriteNode(v.object[k].second, out, depth + 1, indent, pretty);
        if (k + 1 < v.object.size())
            out.push_back(',');
        if (pretty)
            out.push_back('\n');
    }
    if (pretty)
        out.append((size_t)(depth * indent), ' ');
    out.push_back('}');
}

static void WriteNode(const Value &v, std::string &out, int depth, int indent, bool pretty)
{
    switch (v.type)
    {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += v.b ? "true" : "false"; break;
        case Type::Number: out += FormatNumber(v.num); break;
        case Type::String: AppendEscaped(out, v.str); break;
        case Type::Array:  WriteArray(v, out, depth, indent, pretty); break;
        case Type::Object: WriteObject(v, out, depth, indent, pretty); break;
    }
}

std::string Write(const Value &value)
{
    std::string out;
    WriteNode(value, out, 0, 0, false);
    return out;
}

std::string WritePretty(const Value &value, int indent)
{
    std::string out;
    WriteNode(value, out, 0, indent, true);
    return out;
}

} // namespace json
