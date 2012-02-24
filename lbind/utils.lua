local M = {}

local function expandtab(s)
    local space, line = s :match "^(%s*)(.-)%s*$"
    local col = 0
    string.gsub(space, '.', function(s)
        if s == "\t" then
            col = math.ceil((col + 1)/8) * 8
        else
            col = col + 1
        end
    end)
    return (" "):rep(col)..line
end

local function splitlines(t, s)
    if type(s) == 'table' then s = table.concat(s) end
    local indent = expandtab(s):match "^(%s*)"
    s = string.gsub(s, "(.-)\r*\n", function(s)
        local line = expandtab(s):match("^"..indent.."(.-)%s*$")
        t[#t+1] = line or s
        return ""
    end)
    local s = expandtab(s)
    return t, s:match("^"..indent.."(.-)$") or s:match "^%s*(.-)%s*$"
end

--- Output String builder constructoer.
-- this function is used to create a string builder with a table as
-- string pool.
--
--     local pool = {}
--     local L = utils.builder(pool)
--
-- a string builder is used to create a big string, somethimes used to
-- create program text, a string builder is a function, that accept a
-- number or a string:
--  - if a number is given, it will added to indent the lines in
--    builder.
--      L(4) -- indent is 4
--      L(4) -- indent is 8
--      L(-4) -- indent is 4
--  - if a string is given, it will be appended to current line, and a
--    appender will be returned, that means you can use chained
--    expression:
--      L"hello" " " "World" "!" -- a line "Hello World!"
--  - every call to builder start a new line:
--      L"Hello"
--      L"World" -- two line "Hello\nWorld"
-- @param t a string pool used to contain string pieces, to get the
-- big string itself, just use table.concat(t).
function M.builder(t)
    t = t or {}
    local lvl = 0
    local function appender(...)
        t[#t] = t[#t] .. table.concat {...}
        local s = t[#t]
        if s :match "\n" then
            local lvls = (" "):rep(lvl)
            local last = #t
            s = s:sub(lvl+1)
            t[#t] = nil
            local t, remains = splitlines(t, s)
            for i = last, #t do
                t[i] = t[i]:match "^%s*$" and "" or lvls..t[i]
            end
            if remains ~= "" then
                t[#t+1] = lvls..remains
            end
        end
        return appender
    end
    local function header(indent, ...)
        if type(indent) == "number" then
            lvl = lvl + indent
            return header
        end
        if type(indent) == 'table' then
            local lvls = (" "):rep(lvl)
            for k, v in ipairs(indent) do
                t[#t + 1] = lvls..v
            end
            if t[#t] == "" then
                t[#t] = nil
            end
            return header
        end
        if not indent then return t end
        local preline = t[#t]
        if preline and preline :match "^%s*$" then
            t[#t] = ""
        end
        t[#t + 1] = lvl and (" "):rep(lvl) or ""
        return appender(indent, ...)
    end
    return header, appender
end

function M.getlines(s)
    local t, remains = splitlines({}, s)
    if remains ~= "" then
        t[#t + 1] = remains
    end
    return t
end

function M.indent(idt, s)
    local idts = (" "):rep(idt)
    local s = table.concat(M.getlines(s), "\n"..idts)
    if s ~= "" then
        s = idts .. s
    end
    return s--:match "^\n*(.-)[ \n]*$"
end

function M.trim(s)
    if s:match "\n" then
        local t, remains = splitlines({}, s)
        return table.concat(t, "\n").."\n"..remains
    end
    return s:match "^%s*(.-)%s*$"
end

local function template(tpl, info, blacklist, cache)
    local function helper(s)
        if not info[s] or blacklist[s] then return end
        if cache and cache[s] then return cache[s] end
        blacklist[s] = true
        local ret = template(info[s], info, blacklist)
        if cache then cache[s] = ret end
        blacklist[s] = nil
        return ret
    end
    if type(tpl) ~= 'string' or not tpl :match "%$" then
        return tpl
    end
    return (tpl:gsub("${(%w+)}", helper)
               :gsub("$(%w+)", helper))
end

function M.template(tpl, info)
    return template(tpl, info, {}, {})
end

local function table_tostring(t, lvl)
    local ttype = type(t)
    if ttype == 'string' then
        return ('%q'):format(t)
    elseif ttype == 'table' then
        local lvlstring = ('  '):rep(lvl)
        local lvl2string = ('  '):rep(lvl+1)
        local st = {"{\n"}
        for k, v in pairs(t) do
            local cur = #st
            st[cur + 1] = lvl2string
            st[cur + 2] = "["
            st[cur + 3] = table_tostring(k, lvl+1)
            st[cur + 4] = "] = "
            st[cur + 5] = table_tostring(v, lvl+1)
            st[cur + 6] = ",\n"
        end
        st[#st + 1] = lvlstring
        st[#st + 1] = "}"
        return table.concat(st)
    end
    return tostring(t)
end

function M.tostring(t)
    return table_tostring(t, 0)
end

return M
