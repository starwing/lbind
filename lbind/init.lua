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
        return self
    end
end

local function stringmethod(name)
    return function(self, value)
        self[name] = utils.trim(value)
        return self
    end
end

local function vamethod(name, new)
    return function(self, ...)
        local t = {...}
        local cur
        local last = #self
        if new or last == 0 then
            cur = {}
            self[last+1] = cur
        else
            cur = self[last]
        end
        if cur then
            cur[name] = t
        end
        return self
    end
end

local function aliasmethod(self, name)
    local names = self.names
    if not names then
        names = {}
        self.names = names
    end
    names[#names+1] = name
    return self
end

local funcMT = {
    alias = aliasmethod,
    args = vamethod('args', 'new'),
    rets = vamethod 'rets',
    body = codemethod 'body',
    call = codemethod 'call',
    post = codemethod 'post',
    prev = codemethod 'prev',
    cname = stringmethod 'cname',
    lname = stringmethod 'lname',
}
funcMT.__index = funcMT
funcMT.__call = funcMT.args

local function func(name, tag)
    return setmetatable({
        name = name,
        tag = tag or "func",
    }, funcMT)
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

return M
