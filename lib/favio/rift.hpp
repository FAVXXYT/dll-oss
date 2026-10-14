#pragma once
//
// FAVIO compatibility header: minimal rift-compatible expression evaluator.
//
// The original `rift` expression library (EclipseMenu/rift v2.0.2) is no longer
// available, so FAVIO ships this header-only reimplementation covering exactly
// the API surface used by ExpressionFormat.hpp:
//
//   rift::Value / rift::Object / rift::Config / rift::format
//   geode::Result / geode::Ok / geode::Err
//
// Expression grammar (superset of what HUD labels use):
//   ternary  := or ('?' ternary ':' ternary)?
//   or       := and ('||' and)*
//   and      := equality ('&&' equality)*
//   equality := relational (('==' | '!=') relational)*
//   relational := additive (('>=' | '<=' | '>' | '<') additive)*
//   additive := multiplicative (('+' | '-') multiplicative)*
//   multiplicative := unary (('*' | '/' | '%') unary)*
//   unary    := ('-' | '!' | '+') unary | primary
//   primary  := number | string | ident | ident '(' args ')' | '(' ternary ')'
//
// Notes:
// - `if(cond, a, b)` is a builtin special form (unknown names otherwise error).
// - `+` concatenates when either side is a string, else adds numerically.
// - Evaluation is eager (both branches evaluated). All shipped HUD labels are
//   pure, so this is behavior-preserving for every real input.
// - Any error inside a {block} fails the whole format() call, and the caller
//   (ExpressionFormat::format) falls back to the original string unchanged.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace geode {

// Minimal Result<T>: only what ExpressionFormat.hpp uses (isOk/unwrap + Ok/Err).
template <typename T>
class Result {
public:
    static Result ok(T value) {
        Result r;
        r.m_ok = true;
        r.m_value = std::move(value);
        return r;
    }
    static Result err(std::string message) {
        Result r;
        r.m_ok = false;
        r.m_error = std::move(message);
        return r;
    }
    bool isOk() const { return m_ok; }
    const T& unwrap() const {
        if (!m_ok) throw std::logic_error("geode::Result::unwrap on Err: " + m_error);
        return m_value;
    }
    const std::string& unwrapErr() const { return m_error; }

private:
    bool m_ok = false;
    T m_value{};
    std::string m_error;
};

template <typename T>
inline Result<T> Ok(T value) {
    return Result<T>::ok(std::move(value));
}

} // namespace geode

namespace rift {

class Value {
public:
    enum class Type { Int, Float, String, Bool };

    Value() : m_type(Type::Int), m_int(0) {}
    Value(int64_t v) : m_type(Type::Int), m_int(v) {}
    Value(int v) : m_type(Type::Int), m_int(static_cast<int64_t>(v)) {}
    Value(double v) : m_type(Type::Float), m_float(v) {}
    Value(bool v) : m_type(Type::Bool), m_bool(v) {}
    Value(const std::string& v) : m_type(Type::String), m_string(v) {}
    Value(std::string&& v) : m_type(Type::String), m_string(std::move(v)) {}
    Value(const char* v) : m_type(Type::String), m_string(v ? v : "") {}

    Type type() const { return m_type; }

    double toFloat() const {
        switch (m_type) {
            case Type::Int: return static_cast<double>(m_int);
            case Type::Float: return m_float;
            case Type::Bool: return m_bool ? 1.0 : 0.0;
            case Type::String: {
                try {
                    size_t pos = 0;
                    double v = std::stod(m_string, &pos);
                    if (pos == m_string.size()) return v;
                    return 0.0;
                } catch (...) {
                    return 0.0;
                }
            }
        }
        return 0.0;
    }

    std::string toString() const {
        switch (m_type) {
            case Type::Int: return std::to_string(m_int);
            case Type::Bool: return m_bool ? "true" : "false";
            case Type::String: return m_string;
            case Type::Float: return formatDouble(m_float);
        }
        return "";
    }

    bool isTruthy() const {
        switch (m_type) {
            case Type::Int: return m_int != 0;
            case Type::Float: return m_float != 0.0;
            case Type::Bool: return m_bool;
            case Type::String: return !m_string.empty();
        }
        return false;
    }

    static std::string formatDouble(double v) {
        if (std::isnan(v)) return "nan";
        if (!std::isfinite(v)) return v > 0.0 ? "inf" : "-inf";
        double rounded = std::round(v);
        if (v == rounded && std::fabs(v) < 9.0e15) {
            return std::to_string(static_cast<int64_t>(rounded));
        }
        std::ostringstream oss;
        oss.precision(10);
        oss << std::fixed << v;
        std::string s = oss.str();
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

private:
    Type m_type;
    int64_t m_int = 0;
    double m_float = 0.0;
    bool m_bool = false;
    std::string m_string;
};

using Object = std::map<std::string, Value>;
using RuntimeFuncResult = geode::Result<Value>;
using RuntimeFunc = std::function<RuntimeFuncResult(std::span<Value>)>;

class Config {
public:
    static Config& get() {
        static Config instance;
        return instance;
    }
    void registerFunction(const std::string& name, RuntimeFunc func) {
        m_funcs[name] = std::move(func);
    }
    const RuntimeFunc* findFunction(const std::string& name) const {
        auto it = m_funcs.find(name);
        return it == m_funcs.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, RuntimeFunc> m_funcs;
};

namespace detail {

struct EvalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Parser {
public:
    Parser(const std::string& text, const Object& vars, const Config& config)
        : m_text(text), m_vars(vars), m_config(config) {}

    Value parse() {
        Value v = parseTernary();
        skipWs();
        if (m_pos != m_text.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    const std::string& m_text;
    const Object& m_vars;
    const Config& m_config;
    size_t m_pos = 0;

    [[noreturn]] void fail(const std::string& msg) {
        throw EvalError(msg + " at offset " + std::to_string(m_pos));
    }
    void skipWs() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
    }
    char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }
    char peekAt(size_t off) const {
        size_t i = m_pos + off;
        return i < m_text.size() ? m_text[i] : '\0';
    }
    bool consume(char c) {
        if (peek() == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    Value parseTernary() {
        Value cond = parseOr();
        skipWs();
        if (consume('?')) {
            Value whenTrue = parseTernary();
            skipWs();
            if (!consume(':')) fail("expected ':' in ternary expression");
            Value whenFalse = parseTernary();
            return cond.isTruthy() ? whenTrue : whenFalse;
        }
        return cond;
    }

    Value parseOr() {
        Value left = parseAnd();
        for (;;) {
            skipWs();
            if (peek() == '|' && peekAt(1) == '|') {
                m_pos += 2;
                Value right = parseAnd();
                left = Value(left.isTruthy() || right.isTruthy());
            } else {
                return left;
            }
        }
    }

    Value parseAnd() {
        Value left = parseEquality();
        for (;;) {
            skipWs();
            if (peek() == '&' && peekAt(1) == '&') {
                m_pos += 2;
                Value right = parseEquality();
                left = Value(left.isTruthy() && right.isTruthy());
            } else {
                return left;
            }
        }
    }

    Value parseEquality() {
        Value left = parseRelational();
        for (;;) {
            skipWs();
            if (peek() == '=' && peekAt(1) == '=') {
                m_pos += 2;
                Value right = parseRelational();
                left = Value(equalsOp(left, right));
            } else if (peek() == '!' && peekAt(1) == '=') {
                m_pos += 2;
                Value right = parseRelational();
                left = Value(!equalsOp(left, right));
            } else {
                return left;
            }
        }
    }

    static bool equalsOp(const Value& a, const Value& b) {
        if (a.type() == Value::Type::String && b.type() == Value::Type::String) {
            return a.toString() == b.toString();
        }
        return a.toFloat() == b.toFloat();
    }

    Value parseRelational() {
        Value left = parseAdditive();
        for (;;) {
            skipWs();
            if (peek() == '>' && peekAt(1) == '=') {
                m_pos += 2;
                Value right = parseAdditive();
                left = Value(left.toFloat() >= right.toFloat());
            } else if (peek() == '<' && peekAt(1) == '=') {
                m_pos += 2;
                Value right = parseAdditive();
                left = Value(left.toFloat() <= right.toFloat());
            } else if (peek() == '>') {
                ++m_pos;
                Value right = parseAdditive();
                left = Value(left.toFloat() > right.toFloat());
            } else if (peek() == '<') {
                ++m_pos;
                Value right = parseAdditive();
                left = Value(left.toFloat() < right.toFloat());
            } else {
                return left;
            }
        }
    }

    Value parseAdditive() {
        Value left = parseMultiplicative();
        for (;;) {
            skipWs();
            if (peek() == '+') {
                ++m_pos;
                Value right = parseMultiplicative();
                left = addOp(left, right);
            } else if (peek() == '-') {
                ++m_pos;
                Value right = parseMultiplicative();
                left = subOp(left, right);
            } else {
                return left;
            }
        }
    }

    static Value addOp(const Value& a, const Value& b) {
        if (a.type() == Value::Type::String || b.type() == Value::Type::String) {
            return Value(a.toString() + b.toString());
        }
        // Integer-valued results print without decimals via formatDouble,
        // so plain double math is exact for every HUD-scale value.
        return Value(a.toFloat() + b.toFloat());
    }

    static Value subOp(const Value& a, const Value& b) {
        return Value(a.toFloat() - b.toFloat());
    }

    Value parseMultiplicative() {
        Value left = parseUnary();
        for (;;) {
            skipWs();
            if (peek() == '*') {
                ++m_pos;
                Value right = parseUnary();
                left = Value(left.toFloat() * right.toFloat());
            } else if (peek() == '/') {
                ++m_pos;
                Value right = parseUnary();
                double d = right.toFloat();
                if (d == 0.0) fail("division by zero");
                left = Value(left.toFloat() / d);
            } else if (peek() == '%') {
                ++m_pos;
                Value right = parseUnary();
                if (right.toFloat() == 0.0) fail("modulo by zero");
                if (left.type() == Value::Type::Int && right.type() == Value::Type::Int) {
                    // Integer modulo via floats is exact for in-range values.
                    left = Value(static_cast<int64_t>(std::fmod(left.toFloat(), right.toFloat())));
                } else {
                    left = Value(std::fmod(left.toFloat(), right.toFloat()));
                }
            } else {
                return left;
            }
        }
    }

    Value parseUnary() {
        skipWs();
        if (consume('-')) {
            return Value(-parseUnary().toFloat());
        }
        if (consume('!')) {
            Value v = parseUnary();
            return Value(!v.isTruthy());
        }
        if (consume('+')) {
            return parseUnary();
        }
        return parsePrimary();
    }

    Value parsePrimary() {
        skipWs();
        char c = peek();
        if (c == '(') {
            ++m_pos;
            Value v = parseTernary();
            skipWs();
            if (!consume(')')) fail("expected ')'");
            return v;
        }
        if (c == '"' || c == '\'') {
            return Value(parseString());
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peekAt(1))))) {
            return parseNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return parseIdentOrCall();
        }
        fail("unexpected character");
    }

    std::string parseString() {
        char quote = peek();
        ++m_pos;
        std::string out;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos++];
            if (c == '\\' && m_pos < m_text.size()) {
                char e = m_text[m_pos++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    default: out += e; break;
                }
            } else if (c == quote) {
                return out;
            } else {
                out += c;
            }
        }
        fail("unterminated string literal");
    }

    Value parseNumber() {
        size_t start = m_pos;
        bool isFloat = false;
        while (std::isdigit(static_cast<unsigned char>(peek()))) ++m_pos;
        if (peek() == '.') {
            isFloat = true;
            ++m_pos;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++m_pos;
        }
        if (peek() == 'e' || peek() == 'E') {
            size_t save = m_pos;
            ++m_pos;
            if (peek() == '+' || peek() == '-') ++m_pos;
            if (std::isdigit(static_cast<unsigned char>(peek()))) {
                isFloat = true;
                while (std::isdigit(static_cast<unsigned char>(peek()))) ++m_pos;
            } else {
                m_pos = save;
            }
        }
        std::string tok = m_text.substr(start, m_pos - start);
        try {
            if (!isFloat) {
                return Value(static_cast<int64_t>(std::stoll(tok)));
            }
            return Value(std::stod(tok));
        } catch (...) {
            fail("invalid number");
        }
    }

    Value parseIdentOrCall() {
        size_t start = m_pos;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') ++m_pos;
        std::string name = m_text.substr(start, m_pos - start);
        skipWs();
        if (consume('(')) {
            std::vector<Value> args;
            skipWs();
            if (!consume(')')) {
                for (;;) {
                    args.push_back(parseTernary());
                    skipWs();
                    if (consume(',')) continue;
                    if (consume(')')) break;
                    fail("expected ',' or ')' in argument list");
                }
            }
            return callFunction(name, args);
        }
        auto it = m_vars.find(name);
        if (it == m_vars.end()) fail("unknown variable '" + name + "'");
        return it->second;
    }

    Value callFunction(const std::string& name, std::vector<Value>& args) {
        if (name == "if") {
            if (args.size() != 3) fail("if requires 3 arguments");
            return args[0].isTruthy() ? args[1] : args[2];
        }
        const RuntimeFunc* func = m_config.findFunction(name);
        if (!func) fail("unknown function '" + name + "'");
        RuntimeFuncResult result = (*func)(std::span<Value>(args));
        if (!result.isOk()) fail(result.unwrapErr());
        return result.unwrap();
    }
};

// Find the matching close brace for the '{' at openPos, aware of nesting,
// string literals and escapes. Returns npos when unbalanced.
inline size_t findBlockEnd(const std::string& text, size_t openPos) {
    size_t depth = 0;
    char quote = '\0';
    for (size_t i = openPos; i < text.size(); ++i) {
        char c = text[i];
        if (quote != '\0') {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0) return i;
        }
    }
    return std::string::npos;
}

inline geode::Result<std::string> formatImpl(const std::string& templ, const Object& vars) {
    try {
        Config& config = Config::get();
        std::string out;
        out.reserve(templ.size());
        size_t i = 0;
        while (i < templ.size()) {
            if (templ[i] != '{') {
                out += templ[i++];
                continue;
            }
            size_t end = findBlockEnd(templ, i);
            if (end == std::string::npos) {
                return geode::Result<std::string>::err("unbalanced '{' in format string");
            }
            std::string inner = templ.substr(i + 1, end - i - 1);
            Parser parser(inner, vars, config);
            out += parser.parse().toString();
            i = end + 1;
        }
        return geode::Result<std::string>::ok(std::move(out));
    } catch (const EvalError& e) {
        return geode::Result<std::string>::err(e.what());
    } catch (const std::exception& e) {
        return geode::Result<std::string>::err(e.what());
    }
}

} // namespace detail

inline geode::Result<std::string> format(const std::string& formatString, const Object& vars) {
    return detail::formatImpl(formatString, vars);
}

} // namespace rift

namespace geode {
// Err helper producing the RuntimeFuncResult used by registered functions.
inline rift::RuntimeFuncResult Err(std::string message) {
    return rift::RuntimeFuncResult::err(std::move(message));
}
} // namespace geode
