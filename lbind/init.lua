package.path = package.path .. ";../?.lua"
local utils = require 'lbind.utils'
local M = {}

local function body(name, tag)
    return function(body)
        body.name = name
        body.tag = tag
        return body
    end
end

local function code(string, tag)
    local t = utils.getlines(string)
    t.tag = tag or "code"
    return t
end

local function codemethod(name)
    return function(self, string)
        local t = code(string, name)
        local last = #self
        if self[last] then
            self[last][name] = t
        end
        return t
    end
end

local function vamethod(name, new)
    return function(self, ...)
        local t = {...}
        local cur
        if new then
            cur = {}
            self[#self+1] = cur
        else
            cur = self[#self]
        end
        if cur then
            cur[name] = t
        end
        return self
    end
end

local function aliasmethod(self, name)
    local alias = self.alias
    if not alias then
        alias = {}
        self.alias = alias
    end
    alias[#alias+1] = name
    return self
end

local function boolmethod(name, global)
    return function(self, flag, value)
        if value == nil then
            value = true
        end
        if global then
            self[flag] = value
        else
            local last = #self
            if self[last] then
                self[last][flag] = value
            end
        end
        return self
    end
end

local funcMT = {
    alias = aliasmethod,
    args = vamethod 'args',
    rets = vamethod 'rets',
    call = codemethod 'call',
    code = codemethod 'code',
    post = codemethod 'post',
    prev = codemethod 'prev',
}
funcMT.__index = funcMT
funcMT.__call = funcMT.args

local function func(name, tag)
    return setmetatable({tag = tag or "func"}, funcMT)
end

local function var(ctype, tag)
    return function(name)
        return function(body)
            body.tag = tag or 'var'
            body.type = ctype
            body.name = name
            return body
        end
    end
end

function M.export(t)
    for k, v in pairs(M) do
        t[k] = v
    end
    return M
end

function M.module(name)
    return body(name, 'module')
end

function M.subfiles(list, ...)
    if type(list) ~= 'table' then
        list = {list, ...}
    end
    list.tag = "subfiles"
    return list
end

function M.include(file)
    return {
        tag = 'code';
        "#include <"..file..">";
    }
end

function M.include_local(file)
    return {
        tag = 'code';
        "#include \""..file.."\"";
    }
end

function M.code(string)
    return code(string, "code")
end

function M.lua(string)
    return code(string, "lua")
end

function M.object(name)
    return body(name, "object")
end

function M.method(name)
    return func(name, "method")
end

function M.func(name)
    return func(name)
end

function M.field(ctype)
    return var(ctype, "field")
end

function M.var(ctype)
    return var(ctype)
end

return M
