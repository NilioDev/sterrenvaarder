// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/lua32.hpp"

#include <charconv>
#include <cstdio>

namespace sterrenvaarder {

namespace {

const LuaValue kNil;

// Opcodes of the Lua 3.2 virtual machine that table-building chunks use.
enum Op : std::uint8_t {
    ENDCODE = 0x00,
    PUSHNIL = 0x04,
    POP = 0x05,
    PUSHNUMBERW = 0x06,
    PUSHNUMBER = 0x07,
    PUSHNUMBERNEGW = 0x08,
    PUSHNUMBERNEG = 0x09,
    PUSHCONSTANTW = 0x0A,
    PUSHCONSTANT = 0x0B,
    GETGLOBALW = 0x0E,
    GETGLOBAL = 0x0F,
    CREATEARRAYW = 0x15,
    CREATEARRAY = 0x16,
    SETGLOBALW = 0x18,
    SETGLOBAL = 0x19,
    SETLISTW = 0x1C,
    SETLIST = 0x1D,
    SETMAP = 0x1E,
    ADDOP = 0x25,
    SUBOP = 0x26,
    MULTOP = 0x27,
    DIVOP = 0x28,
    MINUSOP = 0x2B,
};

constexpr std::size_t kFieldsPerFlush = 64;  // SETLIST block size

class Reader {
public:
    explicit Reader(Bytes d) : d_(d) {}
    std::uint8_t byte() {
        need(1);
        return d_[p_++];
    }
    std::uint32_t word() {  // big-endian 16 bits
        need(2);
        std::uint32_t v = (d_[p_] << 8) | d_[p_ + 1];
        p_ += 2;
        return v;
    }
    std::uint32_t u32() {  // big-endian 32 bits
        std::uint32_t hi = word();
        return (hi << 16) | word();
    }
    Bytes block(std::size_t n) {
        need(n);
        Bytes b = d_.subspan(p_, n);
        p_ += n;
        return b;
    }
    std::string string() {  // u32 size including the terminating NUL; 0 = none
        std::uint32_t n = u32();
        if (n == 0) return {};
        Bytes b = block(n);
        return std::string(reinterpret_cast<const char*>(b.data()), n - 1);
    }

private:
    void need(std::size_t n) const {
        if (n > d_.size() - p_) throw FormatError("Lua chunk ends early");
    }
    Bytes d_;
    std::size_t p_ = 0;
};

double parse_number(std::string_view s) {
    double v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc()) throw FormatError("bad number constant in Lua chunk: " + std::string(s));
    return v;
}

}  // namespace

const LuaValue* LuaTable::find(std::string_view key) const {
    for (auto& [k, v] : fields)
        if (k == key) return &v;
    return nullptr;
}

void LuaTable::set(const LuaValue& key, LuaValue value) {
    if (key.is_number() && key.number >= 1 && key.number == static_cast<double>(static_cast<std::size_t>(key.number))) {
        auto i = static_cast<std::size_t>(key.number);
        if (i <= array.size() + 1 && i < (1u << 20)) {
            if (i > array.size()) array.resize(i);
            array[i - 1] = std::move(value);
            return;
        }
    }
    std::string k;
    if (key.is_string() || key.is_symbol()) k = key.string;
    else if (key.is_number()) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.17g", key.number);
        k = buf;
    } else throw FormatError("unsupported table key in Lua chunk");
    for (auto& [fk, fv] : fields)
        if (fk == k) { fv = std::move(value); return; }
    fields.emplace_back(std::move(k), std::move(value));
}

const LuaValue& LuaValue::operator[](std::string_view key) const {
    if (!is_table()) return kNil;
    auto v = table->find(key);
    return v ? *v : kNil;
}

const LuaValue& LuaValue::operator[](std::size_t index) const {
    if (!is_table() || index == 0 || index > table->array.size()) return kNil;
    return table->array[index - 1];
}

std::size_t LuaValue::size() const { return is_table() ? table->array.size() : 0; }

const LuaValue& LuaChunk::global(std::string_view name) const {
    for (auto& [k, v] : globals)
        if (k == name) return v;
    return kNil;
}

LuaChunk LuaChunk::parse(Bytes data) {
    Reader r(data);
    if (r.byte() != 0x1B || r.byte() != 'L' || r.byte() != 'u' || r.byte() != 'a') throw FormatError("not a Lua chunk");
    if (r.byte() != 0x32) throw FormatError("not a Lua 3.2 chunk");
    if (r.byte() != 0) throw FormatError("Lua chunk with binary numbers is not supported");
    // Main function: line, source name, code, locals, constants, nested functions.
    r.u32();
    r.string();
    std::uint32_t code_size = r.u32();
    Bytes code = r.block(code_size);
    std::uint32_t nlocals = r.u32();
    for (std::uint32_t i = 0; i < nlocals; ++i) {
        r.u32();
        r.string();
    }
    std::uint32_t nconst = r.u32();
    std::vector<LuaValue> k(nconst);
    for (auto& c : k) {
        std::uint8_t tag = r.byte();
        if (tag == 1) {  // number as text with a byte length
            std::uint8_t n = r.byte();
            Bytes b = r.block(n);
            c.type = LuaValue::Type::Number;
            c.number = parse_number(std::string_view(reinterpret_cast<const char*>(b.data()), n));
        } else if (tag == 2) {
            c.type = LuaValue::Type::String;
            c.string = r.string();
        } else {
            throw FormatError("Lua chunk with nested functions is not supported");
        }
    }

    LuaChunk out;
    std::vector<LuaValue> stack;
    auto pop = [&]() {
        if (stack.empty()) throw FormatError("Lua stack underflow");
        LuaValue v = std::move(stack.back());
        stack.pop_back();
        return v;
    };
    auto constant = [&](std::uint32_t i) -> const LuaValue& {
        if (i >= k.size()) throw FormatError("Lua constant index out of range");
        return k[i];
    };
    auto set_global = [&](const std::string& name, LuaValue v) {
        for (auto& [gk, gv] : out.globals)
            if (gk == name) { gv = std::move(v); return; }
        out.globals.emplace_back(name, std::move(v));
    };
    auto get_global = [&](const std::string& name) {
        for (auto& [gk, gv] : out.globals)
            if (gk == name) return gv;
        LuaValue s;
        s.type = LuaValue::Type::Symbol;
        s.string = name;
        return s;
    };
    auto set_list = [&](std::size_t block, std::size_t n) {
        if (stack.size() < n + 1) throw FormatError("Lua stack underflow in list");
        LuaValue& t = stack[stack.size() - n - 1];
        if (!t.is_table()) throw FormatError("SETLIST on a non-table");
        for (std::size_t i = 0; i < n; ++i) {
            LuaValue key;
            key.type = LuaValue::Type::Number;
            key.number = static_cast<double>(block * kFieldsPerFlush + i + 1);
            t.table->set(key, std::move(stack[stack.size() - n + i]));
        }
        stack.resize(stack.size() - n);
    };

    std::size_t pc = 2;  // stack size and parameter count come first
    if (code.size() < 2) throw FormatError("empty Lua code");
    auto b = [&]() -> std::uint32_t {
        if (pc >= code.size()) throw FormatError("Lua code ends early");
        return code[pc++];
    };
    auto w = [&]() -> std::uint32_t {
        std::uint32_t hi = b();
        return (hi << 8) | b();
    };
    for (int steps = 0; steps < 10'000'000; ++steps) {
        if (pc >= code.size()) throw FormatError("Lua code without ENDCODE");
        std::uint8_t op = code[pc++];
        switch (op) {
            case ENDCODE: return out;
            case PUSHNIL: {
                std::uint32_t n = b() + 1;
                for (std::uint32_t i = 0; i < n; ++i) stack.emplace_back();
                break;
            }
            case POP: {
                std::uint32_t n = b();
                if (n > stack.size()) throw FormatError("Lua stack underflow");
                stack.resize(stack.size() - n);
                break;
            }
            case PUSHNUMBERW:
            case PUSHNUMBER:
            case PUSHNUMBERNEGW:
            case PUSHNUMBERNEG: {
                double v = (op == PUSHNUMBERW || op == PUSHNUMBERNEGW) ? w() : b();
                LuaValue n;
                n.type = LuaValue::Type::Number;
                n.number = (op == PUSHNUMBERNEGW || op == PUSHNUMBERNEG) ? -v : v;
                stack.push_back(std::move(n));
                break;
            }
            case PUSHCONSTANTW: stack.push_back(constant(w())); break;
            case PUSHCONSTANT: stack.push_back(constant(b())); break;
            case GETGLOBALW: stack.push_back(get_global(constant(w()).string)); break;
            case GETGLOBAL: stack.push_back(get_global(constant(b()).string)); break;
            case CREATEARRAYW:
            case CREATEARRAY: {
                (op == CREATEARRAYW) ? w() : b();
                LuaValue t;
                t.type = LuaValue::Type::Table;
                t.table = std::make_shared<LuaTable>();
                stack.push_back(std::move(t));
                break;
            }
            case SETGLOBALW: {
                std::string name = constant(w()).string;
                set_global(name, pop());
                break;
            }
            case SETGLOBAL: {
                std::string name = constant(b()).string;
                set_global(name, pop());
                break;
            }
            case SETLISTW: {
                std::uint32_t block = w();
                set_list(block, b());
                break;
            }
            case SETLIST: {
                std::uint32_t block = b();
                set_list(block, b());
                break;
            }
            case SETMAP: {
                std::size_t n = b() + 1;
                if (stack.size() < 2 * n + 1) throw FormatError("Lua stack underflow in map");
                LuaValue& t = stack[stack.size() - 2 * n - 1];
                if (!t.is_table()) throw FormatError("SETMAP on a non-table");
                for (std::size_t i = 0; i < n; ++i) {
                    std::size_t at = stack.size() - 2 * n + 2 * i;
                    t.table->set(stack[at], std::move(stack[at + 1]));
                }
                stack.resize(stack.size() - 2 * n);
                break;
            }
            case ADDOP:
            case SUBOP:
            case MULTOP:
            case DIVOP: {
                LuaValue rhs = pop(), lhs = pop();
                LuaValue res;
                if (lhs.is_number() && rhs.is_number()) {
                    res.type = LuaValue::Type::Number;
                    res.number = op == ADDOP ? lhs.number + rhs.number : op == SUBOP ? lhs.number - rhs.number
                               : op == MULTOP ? lhs.number * rhs.number : lhs.number / rhs.number;
                } else if (op == ADDOP && lhs.is_symbol() && rhs.is_symbol()) {
                    // Flag sets such as LIT_DYNAMIC + LIT_AMBIENT stay symbolic: "A+B".
                    res.type = LuaValue::Type::Symbol;
                    res.string = lhs.string + "+" + rhs.string;
                } else {
                    throw FormatError("unsupported arithmetic in Lua chunk");
                }
                stack.push_back(std::move(res));
                break;
            }
            case MINUSOP: {
                LuaValue v = pop();
                if (!v.is_number()) throw FormatError("unsupported negation in Lua chunk");
                v.number = -v.number;
                stack.push_back(std::move(v));
                break;
            }
            default: {
                char buf[64];
                std::snprintf(buf, sizeof buf, "unsupported Lua opcode 0x%02X at %zu", op, pc - 1);
                throw FormatError(buf);
            }
        }
    }
    throw FormatError("Lua code too long");
}

std::string to_lua_text(const LuaValue& v, int indent) {
    char buf[40];
    switch (v.type) {
        case LuaValue::Type::Nil: return "nil";
        case LuaValue::Type::Number: std::snprintf(buf, sizeof buf, "%.9g", v.number); return buf;
        case LuaValue::Type::Symbol: return v.string;
        case LuaValue::Type::String: {
            std::string s = "\"";
            for (char c : v.string) s += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
            return s + "\"";
        }
        case LuaValue::Type::Table: break;
    }
    // Short tables of plain values on one line, others indented.
    bool flat = v.table->fields.empty() && v.table->array.size() <= 9;
    for (auto& e : v.table->array) flat = flat && !e.is_table();
    if (flat) {
        std::string s = "{";
        for (std::size_t i = 0; i < v.table->array.size(); ++i) s += (i ? ", " : "") + to_lua_text(v.table->array[i]);
        return s + "}";
    }
    std::string pad(static_cast<std::size_t>(indent + 1) * 2, ' '), s = "{\n";
    for (auto& e : v.table->array) s += pad + to_lua_text(e, indent + 1) + ",\n";
    for (auto& [k, e] : v.table->fields) s += pad + k + " = " + to_lua_text(e, indent + 1) + ",\n";
    return s + std::string(static_cast<std::size_t>(indent) * 2, ' ') + "}";
}

}  // namespace sterrenvaarder
