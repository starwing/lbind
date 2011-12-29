local M = {}
local types = {}
local utils = require 'lbind.utils'

local function typeref(t)
    local refinfo = {}

    -- some fallbacks
    setmetatable(refinfo, refinfo)
    function refinfo:__index(k)
        local self = t:info()
        if k == 'type' or k == 'ctype' then
            return self.ctype or self.typename
        elseif k == 'ltype' then
            return self.ltype or self.typename
        end
        return self[k]
    end

    local ref = {}

    function ref:ref(name, idx)
        if not refinfo.name then
            refinfo.name = idx and name..idx or name
            refinfo.arg = utils.template(refinfo.arg_tpl, refinfo)
        end
        refinfo.idx = idx
        return self
    end
    function ref:opt(value)
        refinfo.opt = true
        refinfo.default = value
        return self
    end

    function ref:isopt() return refinfo.opt end
    function ref:optvalue() return refinfo.default or refinfo.initvalue end
    function ref:opt_check() return refinfo.opt_check_tpl end
    function ref:name() return refinfo.name end
    function ref:idx() return refinfo.idx end
    function ref:ctype() return refinfo.ctype end
    function ref:ltype() return refinfo.ltype end
    function ref:info() return refinfo end

    function ref:gen_arg()
        return refinfo.arg
    end
    function ref:gen_decl(initvalue)
        refinfo.initvalue = initvalue
        local ret = utils.template(refinfo.decl_tpl, refinfo)..
                    utils.template(refinfo.decl_arg, refinfo)
        refinfo.initvalue = nil
        return ret
    end
    function ref:gen_assign(value)
        refinfo.value = value
        local ret = utils.template(refinfo.assign_tpl, refinfo)
        refinfo.value = nil
        return ret
    end

    local function gen_template(name)
        ref['gen_'..name] = function(self, narg)
            refinfo.narg = narg
            local ret = utils.template(refinfo[name.."_tpl"], refinfo)
            refinfo.narg = nil
            return ret, refinfo[name.."_nstack"]
        end
    end
    gen_template "is"
    gen_template "to"
    gen_template "check"
    gen_template "opt_check"

    function ref:gen_push()
        return utils.template(refinfo.push_tpl, refinfo), refinfo.push_nstack
    end

    return ref
end

function M.type(name)
    local t = {}
    local info = {
        typename = name,
        initvalue = "0",
        arg_tpl = "$name",
        assign_tpl = "$name = $value",
        decl_tpl = "$type $name",
        decl_arg = " = $initvalue",
    }

    function t:set(key)
        return function(value)
            info.key = value
        end
    end
    function t:ctype(name)
        info.ctype = name
        return self
    end
    function t:ltype(name)
        info.ltype = name
        return self
    end
    function t:initvalue(s)
        info.initvalue = utils.trim(s)
        return self
    end
    function t:arg(s)
        info.arg_tpl = utils.trim(s)
        return self
    end
    function t:assign(s)
        info.assign_tpl = utils.trim(s)
        return self
    end
    function t:decl(decl, arg)
        if decl then info.decl_tpl = utils.trim(decl) end
        if arg then info.decl_arg = utils.trim(arg) end
        return self
    end

    local function collect_template(name)
        t[name] = function(self, nstack)
            if type(nstack) == 'string' then
                info[name.."_tpl"] = utils.trim(nstack)
                info[name.."_nstack"] = 1
                return self
            end
            return function(s)
                info[name.."_tpl"] = utils.trim(s)
                info[name.."_nstack"] = nstack
                return self
            end
        end
    end
    collect_template 'is'
    collect_template 'to'
    collect_template 'check'
    collect_template 'opt_check'
    collect_template 'push'

    function t:info()
        return info
    end

    setmetatable(t, t)

    function t:__call(name)
        return typeref(self):ref(name)
    end
    function t:ref(name, idx)
        return typeref(self):ref(name, idx)
    end
    function t:opt(value)
        return typeref(self):opt(value)
    end

    types[name] = t
    return t
end

function M.callback(name)
end

function M.classtype(name)
    local t = M.type(name)


    return t
end

function M.basetypes(t)
    t.int = M.type "int" :ltype "integer"
        :is [[ lua_isnumber(L, $narg) ]]
        :push [[ lua_pushinteger(L, $name) ]]
        :opt_check [[ luaL_optint(L, $narg) ]]
        :check [[ luaL_checkint(L, $narg) ]]
        :to [[ lua_tointeger(L, $narg) ]]
    t.intptr = M.type "intptr" :ltype "integer" :ctype "int"
        :arg [[ &$name ]]
        :is [[ lua_isnumber(L, $narg) ]]
        :push [[ lua_pushinteger(L, $name) ]]
        :to [[ lua_tointeger(L, $narg) ]]
    t.double = M.type "double" :ltype "number"
        :is [[ lbind_isnumber(L, $narg) ]]
        :push [[ lua_pushnumber(L, $name) ]]
        :to [[ lua_tonumber(L, $narg) ]]
    t.cstring = M.type "string" :ltype "string" :ctype "const char *"
        :decl [[ $type$name ]]
        :is [[ lua_isstring(L, $narg) ]]
        :push [[ lua_pushstring(L, $name) ]]
        :to [[ lua_tostring(L, $narg) ]]
    t.lstring = M.type "lstring" :ltype "string" :ctype "const char *"
        :arg [[ $name, ${name}_len ]]
        :decl [[
            size_t ${name}_len;
            $type$name ]]
        :is [[ lua_isstring(L, $narg) ]]
        :push [[ lua_pushlstring(L, $name, ${name}_len) ]]
        :to [[ lua_tolstring(L, $narg, &${name}_len) ]]

    t.char = t.int
end

function M.gettype(name)
    return types[name]
end

local function gen_getargs(_, fn, narg, args)
    local t = {}

    for i, v in ipairs(args) do
        local checkstr, nstack
        if v:isopt() and v:opt_check() then
            checkstr, nstack = v:gen_opt_check(narg)
        else
            checkstr, nstack = v[fn](v, narg)
            if v:isopt() then
                checkstr = ("lua_isnoneornil(L, %d) ? %s : %s")
                    :format(narg, checkstr, v:optvalue())
            end
        end
        _(v:gen_decl(checkstr))";"
        t[#t + 1] = v:gen_arg()
        narg = narg + nstack
    end

    return t
end

function M.gen_getargs(_, narg, args)
    return gen_getargs(_, 'gen_to', narg, args)
end

function M.gen_checkargs(_, narg, args)
    return gen_getargs(_, 'gen_check', narg, args)
end

function M.gen_pushargs(_, args)
    local count = 0
    for i, v in ipairs(args) do
        local pushstr, nstack = v:gen_push()
        _(pushstr)";"
        count = count + nstack
    end
    return count
end

return M
