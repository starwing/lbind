local M = {}
local modules = {}
local classes = {}
local funcs = {}
local vars = {}
local enums = {}
setmetatable(M, {__index = function(self, k)
    return modules[k] or
           classes[k] or
           funcs[k] or
           vars[k] or
           enums[k]
end})

local function export(t, module, name)
    M[name] = function(name)
        local ret = require (module) (name)
        t[name] = ret
        return ret
    end
end

export(classes, "lbind.gen_module", 'class')
export(enums, "lbind.gen_enum", 'enum')
export(funcs, "lbind.gen_func", 'func')
export(modules, "lbind.gen_module", 'module')
export(vars, "lbind.gen_variable", 'variable')

function M.generate(config)
end

return M
