package.path = package.path .. ";../?.lua"
local utils = require 'lbind.utils'
local M = {}
local T = {}

local typeMT, varMT

local function typedecl(name, tag)
    local t = T[name]
    if t then return t end
    t = setmetatable({
        name = name,
        tag = tag or "type",
    }, typeMT)
    T[name] = t
    return t
end

-- methods in metatables

local function rawmethod(name)
    return function(self, value)
        self[name] = value
        return self
    end
end

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

local function find_type(tname, key)
    local t = T[tname]
    if not t then
        t = typedecl(tname, tag)
        t[key] = true
    elseif not t[key] then
        error("imcompatile type: "..tname, 2)
    end
    return t
end

local function qualmethod(qual)
    local key = "is_"..qual
    local prefix = qual.."_"
    return function(self)
        if self[key] then return self end
        return find_type(prefix..self.name, key)
    end
end

local function indirmethod(indir)
    local key = "is_"..indir
    local postfix = "_"..indir:sub(1,1)
    return function(self, name, tag)
        if type(name) == 'string' then -- declare new variable
            local var = self(name, tag)
            var[key] = true
            return var
        end

        if self[key] then return self end
        return find_type(self.name..postfix, key)
    end
end

typeMT = {
    extends   =      rawmethod 'base',
    ctype     =   stringmethod 'type_c',
    ltype     =   stringmethod 'type_lua',
    const     =     qualmethod 'const',
    volatile  =     qualmethod 'volatile',
    ptr       =    indirmethod 'ptr',
    ref       =    indirmethod 'ref',
    arg       =   stringmethod 'arg_tpl',
    assign    =   stringmethod 'assign_tpl',
    initvalue =   stringmethod 'initvalue_tpl',
    check     = templatemethod "check",
    is        = templatemethod "is",
    opt       = templatemethod "opt",
    push      = templatemethod "push",
    to        = templatemethod "to",
}
typeMT.__index = typeMT

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

varMT = {
    const    =   boolmethod 'is_const',
    volatile =   boolmethod 'is_volatile',
    ptr      =   boolmethod "is_ptr",
    ref      =   boolmethod "is_ref",
    opt      = stringmethod "opt_tpl",
}
varMT.__index = varMT

-- using __call, ptr, ref to create var instance.
function typeMT:__call(name, tag)
    return setmetatable({
        tag = tag or "var",
        name = name,
        type = self,
    }, varMT)
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
    return t
end

-- C part
inttype "char"            :ctype "char"
inttype "uchar"           :ctype "unsigned char"
inttype "short"           :ctype "short int"
inttype "ushort"          :ctype "unsigned short int"
inttype "int"             :ctype "int"
inttype "uint"            :ctype "unsigned int"
inttype "long"            :ctype "long int"
inttype "ulong"           :ctype "unsigned long int"
inttype "size_t"
inttype "ssize_t"
fixinttype(8)
fixinttype(8, "u")
fixinttype(16)
fixinttype(16, "u")
fixinttype(32)
fixinttype(32, "u")
fixinttype(64)
fixinttype(64, "u")
numbertype "float"
numbertype "double"
numbertype "ldouble"      :ctype "long double"
stringtype "char_p"       :ctype "char *"
stringtype "const_char_p" :ctype "const char *"
stringtype "uchar_p"      :ctype "unsigned char *"

-- C++ part

function M.export(t)
    for k, v in pairs(M) do
        t[k] = v
    end
    M.basetypes(t)
    return M
end

function M.dyn_export(t)
    local mt = getmetatable(t)
    local old_index = mt.__index
    if old_index then
        if type(old_index) == 'table' then
            function mt:__index(key)
                return old_index[key] or T[key]
            end
        elseif type(old_index) == 'function'then
            function mt:__index(key)
                return old_index(self, key) or T[key]
            end
        end
    else
        mt.__index = T
    end
    return M
end

function M.basetypes(t)
    if t then
        for k, v in pairs(T) do
            t[k] = v
        end
    end
    return T
end

function M.typedecl(name)
    local t = typedecl(name)
    T[name] = t
    return t
end

function M.class(name)
    local t = classtype(name)
    return t
end

M.selfType = classtype "<self>"

return M
