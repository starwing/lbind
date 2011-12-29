local gen_func = require 'lbind.gen_func'
local utils = require 'lbind.utils'
local typeinfo = require 'lbind.typeinfo'

return function(name)
    local t = {}
    local metafuncs = {}
    local methods = {}
    local statics = {}
    local virtuals = {}
    local vars = {}
    local enums = {}

    setmetatable(t, t)
    function t:__index(k)
        return methods[k] or
               statics[k] or
               virtuals[k] or
               metafuncs[k] or
               vars or
               enums
    end

    local function filltable(t, name)
        t[name] = function(self, ...)
            for i = i, select('#', ...) do
                t[#t + 1] = select(i, ...)
            end
            return self
        end
    end

    filltable(metafuncs, 'op')
    filltable(methods, 'methods')
    filltable(virtuals, 'virtuals')
    filltable(statics, 'statics')
    filltable(vars, 'variables')
    filltable(enums, 'enums')

    local function prefixname()
        return prefix and prefix.."_"..name or name
    end

    function t:gen_shell_class(prefix)
    end

    function t:gen_bindings(prefix)
    end

    function t:gen_regtable(prefix)
        local t = {}
        local _ = utils.builder(t)

        _"static luaL_Reg "(prefixname())"_funcs[] = {"
        _"};"
        _""
        _"static luaL_Reg "(prefixname())"_metas[] = {"
        _"};"

        return t
    end

    function t:gen_luaopen(prefix)
        local t = {}
        local _ = utils.builder(t)

        _"LUALIB_API int luaopen_"(prefixname())"(lua_State *L) {"
        _"}"

        return t
    end

    return t
end
