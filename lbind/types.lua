package.path = package.path .. ";../?.lua"
local luatype = type
local utils = require 'lbind.utils'
local M = {}
local B = {}
local T = {}

local function boolmethod(name)
    return function(self, value)
        if value == nil then
            value = true
        end
        self[name] = value
        return self
    end
end

local function stringmethod(name)
    return function(self, value)
        self[name] = utils.trim(value)
        return self
    end
end

local function templatemethod(name)
    return function(self, nstack)
        if type(nstack) == 'string' then
            self[name.."_tpl"] = utils.trim(nstack)
            self[name.."_nstack"] = 1
            return self
        end
        return function(s)
            self[name.."_tpl"] = utils.trim(s)
            self[name.."_nstack"] = nstack
            return self
        end
    end
end

local function cvmethod(cv)
    return function(self)
        local tname = cv.." "..self.name
        local t = B[tname] or T[tname]
        if not t then
            t = self:copy()
            t[cv] = true
            T[tname] = t
        end
        return t
    end
end

local typeMT = {
    ctype     = stringmethod 'ctype',
    ltype     = stringmethod 'ltype',
    const     = cvmethod 'const',
    volatile  = cvmethod 'volatile',
    arg       = stringmethod 'arg',
    assign    = stringmethod 'assign',
    initvalue = stringmethod 'initvalue',
    check     = templatemethod "check",
    is        = templatemethod "is",
    opt       = templatemethod "opt",
    push      = templatemethod "push",
    to        = templatemethod "to",
}
typeMT.__index = typeMT

function typeMT:copy()
    local t = {}
    for k, v in pairs(self) do
        t[k] = v
    end
    return setmetatable(t, typeMT)
end

function typeMT:set(key)
    if type(key) == 'table' then
        for k, v in pairs(key) do
            self[k] = v
        end
    else
        return function(value)
            self[key] = value
            return self
        end
    end
    return self
end

function typeMT:ptr(name, tag)
    local tv = self(name, tag)
    tv.ptr = true
    return tv
end

function typeMT:ref(name, tag)
    local tv = self(name, tag)
    tv.ref = true
    return tv
end

local objMT = {
    ptr = function(self, name) return self.type:ptr(name) end,
    ref = function(self, name) return self.type:ref(name) end,
}
objMT = objMT.__index

-- using __call, ptr, ref to create var instance.
function typeMT:__call(name, tag)
    return setmetatable({
        tag = tag or "var",
        name = name,
        type = self,
    }, objMT)
end

local function typedecl(name, tag)
    local t = B[name] or T[name]
    if t then return t end
    return setmetatable({
        name = name,
        tag = tag or "type",
    }, typeMT)
end

local function inttype(name, ctype)
    return typedecl(name)
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
    return typedecl(name)
        :ctype(ctype or name)
        :ltype "number"
        :is "lua_isnumber(L, $narg)"
        :push "lua_pushnumber(L, $name)"
        :opt "luaL_optnumber(L, $narg, $defaultvalue)"
        :check "luaL_checknumber(L, $narg)"
        :to "($ctype)lua_tonumber(L, $narg)"
end

local function stringtype(name, ctype)
    return typedecl(name)
        :ctype(ctype or name)
        :ltype "string"
        :is "lua_isstring(L, $narg)"
        :push "lua_pushstring(L, $name)"
        :opt "luaL_optstring(L, $narg, $defaultvalue)"
        :check "luaL_checkstring(L, $narg)"
        :to "($ctype)lua_tostring(L, $narg)"
end

local function classtype(name, ctype)
    local t = typedecl(name):ctype(ctype or name)
    T[name] = t
    return t
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
B.const_char_p = stringtype "const char *"
B.char_p = stringtype "char *"
B.uchar_p = stringtype "unsigned char *"

-- C++ part

function M.export(t)
    for k, v in pairs(M) do
        t[k] = v
    end
    M.basetypes(t)
    return M
end

function M.basetypes(t)
    if t then
        for k, v in pairs(B) do
            t[k] = v
        end
    end
    return B
end

function M.typedecl(name)
    local t = typedecl(name)
    B[name] = t
    return t
end

function M.class(name)
    local t = classtype(name)
    T[name] = t
    return t
end

M.selfType = classtype "__self__"

return M
