package.path = package.path .. ";../?.lua"
local utils = require 'lbind.utils'
local M = {}
local B = {}

local typeMT = {}
typeMT.__index = typeMT

local function type(name, tag)
    return setmetatable({tag = tag or "type"}, typeMT)
end

local function inttype(name, ctype)
    return type(name)
        :ctype(ctype or name)
        :ltype "integer"
        :is "lua_isnumber(L, $narg)"
        :push "lua_pushinteger(L, $name)"
        :opt "luaL_optint(L, $narg, $defaultvalue)"
        :check "luaL_checkint(L, $narg)"
        :to "($ctype)lua_tointeger(L, $narg)"
end

local function fixinttype(len, u)
    return inttype((u or "").."int"..len.."_t")
end

local function numbertype(name, ctype)
    return type(name)
        :ctype(ctype or name)
        :ltype "number"
        :is "lua_isnumber(L, $narg)"
        :push "lua_pushnumber(L, $name)"
        :opt "luaL_optnumber(L, $narg, $defaultvalue)"
        :check "luaL_checknumber(L, $narg)"
        :to "($ctype)lua_tonumber(L, $narg)"
end

local function stringtype(name, ctype)
    return type(name)
        :ctype(ctype or name)
        :ltype "string"
        :is "lua_isstring(L, $narg)"
        :push "lua_pushstring(L, $name)"
        :opt "luaL_optstring(L, $narg, $defaultvalue)"
        :check "luaL_checkstring(L, $narg)"
        :to "($ctype)lua_tostring(L, $narg)"
end

-- C part
B.char = inttype "char"
B.uchar = inttype "unsigned char"
B.short = inttype "short int"
B.ushort = inttype "unsigned short int"
B.int = inttype "int"
B.ushort = inttype "unsigned int"
B.long = inttype "long int"
B.ulong = inttype "unsigned long int"
B.size_t = inttype "size_t"
B.ssize_t = inttype "ssize_t"
B.int8_t = fixinttype(8)
B.uint8_t = fixinttype(8, "u")
B.int16_t = fixinttype(16)
B.uint16_t = fixinttype(16, "u")
B.int32_t = fixinttype(32)
B.uint32_t = fixinttype(32, "u")
B.int64_t = fixinttype(64)
B.uint64_t = fixinttype(64, "u")
B.float = numbertype "float"
B.double = numbertype "double"
B.ldouble = numbertype "long double"
B.charp = stringtype "char*"
B.ucharp = stringtype "unsigned char*"

-- C++ part

function M.export(t)
    for k, v in pairs(M) do
        t[k] = v
    end
    M.basetypes(t)
    return M
end

function M.type(name)
    return type(name)
end

function M.basetypes(t)
    if t then
        for k, v in pairs(B) do
            t[k] = v
        end
    end
    return B
end

return M
