local M = {}

function M.export(t)
    for k, v in pairs(M) do
        t[k] = v
    end
    return M
end

function M.module(name)
end

function M.include(file)
end

function M.include_local(file)
end

function M.code(string)
end

function M.lua(string)
end

function M.subfiles(list, ...)
end

function M.object(name)
end

function M.method(name)
end

function M.func(name)
end

function M.field(name)
end

function M.var(name)
end

return M

