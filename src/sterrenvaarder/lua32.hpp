// SPDX-License-Identifier: MPL-2.0
#pragma once
// Reader for the precompiled Lua 3.2 chunks the game uses as data (.thn scene scripts).
// The scripts only build tables and assign globals, so this evaluates that subset of the
// bytecode (constants, table constructors, global reads/writes) and never runs code:
// anything else (calls, functions, arithmetic) is rejected with a FormatError.
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

struct LuaTable;

struct LuaValue {
    enum class Type { Nil, Number, String, Symbol, Table } type = Type::Nil;
    double number = 0;
    std::string string;  // String, or the global's name for Symbol (SCENE, Y_AXIS, ...)
    std::shared_ptr<LuaTable> table;

    bool is_nil() const { return type == Type::Nil; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_symbol() const { return type == Type::Symbol; }
    bool is_table() const { return type == Type::Table; }
    // Field of a table value (nil when this is not a table or the key is missing).
    const LuaValue& operator[](std::string_view key) const;
    const LuaValue& operator[](std::size_t index) const;  // 1-based array part
    std::size_t size() const;                            // array part length
    double num(double fallback = 0) const { return is_number() ? number : fallback; }
    const std::string& str() const { return string; }
};

struct LuaTable {
    std::vector<LuaValue> array;                             // [1], [2], ...
    std::vector<std::pair<std::string, LuaValue>> fields;    // other keys, in order
    const LuaValue* find(std::string_view key) const;
    void set(const LuaValue& key, LuaValue value);
};

// Global assignments of a chunk, in order, with symbols for globals it reads but never set.
struct LuaChunk {
    std::vector<std::pair<std::string, LuaValue>> globals;
    const LuaValue& global(std::string_view name) const;
    static LuaChunk parse(Bytes data);  // throws FormatError
};

// Text form for tools and debugging.
std::string to_lua_text(const LuaValue& v, int indent = 0);

}  // namespace sterrenvaarder
