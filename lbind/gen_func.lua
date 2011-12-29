local utils = require 'lbind.utils'
local typeinfo = require 'lbind.typeinfo'

local function gen_simple_dispatcher(L, funclist)
    L"int top = lua_gettop(L);"
    for i, v in ipairs(funclist) do
        local args = v.args
        local cont = L"if (top >= "(args.opt_idx or #args)
        if args.opt_idx then
            local idx = 1
            for i = 1, args.opt_idx - 1 do
                local isstr, nstack = v:gen_is(idx)
                cont" && "(isstr)
                idx = idx + nstack
            end
        else
            local idx = 1
            for i, v in ipairs(args) do
                local isstr, nstack = v:gen_is(idx)
                cont" && "(isstr)
                idx = idx + nstack
            end
        end
        cont ") return "(v.binder_name)"(L);"
    end
end

local function get_arg_categories(list, idx, limit)
    local t = {}
    local lists = {}
    local types = {}
    idx = idx or 1
    local min
    for i, v in ipairs(list) do
        if not min or min > #v.args then min = #v.args end
        local arg = v.args[idx]
        if not arg then
            t.endnode = v
        else
            if arg:isopt() then
                t.endnode = v
                t.n = idx - 1
            end
            local argtype = arg and arg:ltype()
            if not types[argtype] then
                local newlist = {}
                types[argtype] = newlist
                lists[#lists+1] = newlist
            end
            local tt = types[argtype]
            --if arg:isopt() then
                --if not tt.optional then tt.optional = {curtype = arg} end
                --table.insert(tt.optional, v)
            --else
                if not tt.curtype then tt.curtype = arg end
                table.insert(tt, v)
            --end
        end
    end
    if not limit or limit == 0 then
        limit = limit or min
    end
    for i, v in ipairs(lists) do
        t[i] = get_arg_categories(v, idx + 1, limit - 1)
        t[i].curtype = v.curtype
        --if v.optional then
            --t[i].optional = get_arg_categories(v.optional, idx + 1)
            --t[i].optional.curtype = v.optional.curtype
        --end
    end
    if #t ~= 0 and t.endnode then
        table.insert(t, 1, {
            endnode = t.endnode,
            n = t.n or #t.endnode.args,
        })
        t.endnode, t.n = nil
    end
    return t
end

local function set_limit_condition(cond, list, field, op, gettop)
    if list[field] then
        local top_cond = (gettop or "lua_gettop(L)")..op..list[field]
        cond = cond and cond.." && "..top_cond or top_cond
    end
    return cond
end

local function get_conditions(list, idx, gettop)
    local cond = list.cond
    cond = set_limit_condition(cond, list, 'n', ' == ', gettop)
    cond = set_limit_condition(cond, list, 'min', ' <= ', gettop)
    cond = set_limit_condition(cond, list, 'max', ' >= ', gettop)
    if list.curtype then
        local isstr, nstack = list.curtype:gen_is(idx)
        cond = cond and cond.." && "..isstr or isstr
        idx = idx + nstack
    end
    return cond, idx
end

local function print_tree(list, idx, lvl)
    local lvl = lvl or 0
    local cond, idx = get_conditions(list, idx or 1)
    local lvls = ("  "):rep(lvl)
    local lvls2 = ("  "):rep(lvl + 1)
    print(lvls.."{")
    print(lvls2..(cond or "none"))
    for i, v in ipairs(list) do
        print_tree(v, idx, lvl + 1)
    end
    if list.endnode then
        print(lvls2.."endnode: "..list.endnode.typelist)
    end
    if list.optional then
        print(lvls2..'optionals:')
        for i, v in ipairs(list.optional) do
            print_tree(v, idx, lvl + 1)
        end
    end
    print(lvls.."}")
end

local function gen_conditions(L, list, idx, gettop)
    local brace
    local cond, idx = get_conditions(list, idx, gettop)

    -- if not cond, go though
    while not cond and #list == 1 do
        list = list[1]
        cond, idx = get_conditions(list, idx, gettop)
    end

    -- if it sill has cond and have one branch
    if #list == 1 and cond then
        local C = L"if ("(cond)
        repeat
            list = list[1]

            cond, idx = get_conditions(list, idx, gettop)

            -- still may not have cond
            while not cond and #list == 1 do
                list = list[1]
                cond, idx = get_conditions(list, idx, gettop)
            end

            -- add conditions from the only branch
            if cond then
                C = nil
                L"        && "(cond)
            end
        until #list ~= 1

        brace = (C or L)"   )"

    -- if we have serveral branches
    elseif #list ~= 1 and cond then
        brace = L"if ("(cond)")"
        if #list == 0 then brace = nil end
    end

    if brace then brace" {" L(4) end
    for i, v in ipairs(list) do
        gen_conditions(L, v, idx, gettop)
    end
    if list.optional then
        print(#list.optional)
        for i, v in ipairs(list.optional) do
            gen_conditions(L, v, idx, gettop)
        end
    end
    if list.endnode then
        L"    return "(list.endnode.binder_name)"(L);"
    end
    if brace then L(-4)"}" end
end

local function gen_binder_body(L, cname, func, arglist)
    -- gen return value declarations
    local direct_decl_ret
    local idx = 1
    local rets = func.ret
    local names = func.args.nametable
    if rets then
        if not func.body and not func.pre then
            direct_decl_ret = true
        end
        for k, v in ipairs(rets) do
            if v:name() == "ret" then
                idx = k
                break
            end
        end
        local last_idx
        for i, v in ipairs(rets) do
            if not names[v:name()] and
                    (not direct_decl_ret or idx ~= i) then
                last_idx = i
            end
        end
        if last_idx and last_idx > idx or names[rets[idx]:name()] then
            direct_decl_ret = nil
        end
        for i, v in ipairs(rets) do
            if not names[v:name()] and
                    (not direct_decl_ret or idx ~= i) then
                L(v:gen_decl())";"
            end
        end
    end

    if func.body then
        L(func.body)
    else
        -- gen pre code
        if func.pre then
            L(func.pre)
        end

        local ret = rets and rets[idx]
        local arglist = table.concat(arglist, ", ")
        local callline = cname.."("..arglist..")"
        if func.callline then
            for i, v in ipairs(func.callline) do
                L((v
                    :gsub("$arglist", arglist)
                    :gsub("$calline", callline)
                 ))
            end
        elseif direct_decl_ret then
            L(ret:gen_decl(callline))";"
        elseif rets then
            L(ret:gen_assign(callline))";"
        else
            L(callline)";"
        end

        -- gen post code
        if func.post then
            L(func.post)
        end
    end

    -- push return values
    if rets then
        local nret = typeinfo.gen_pushargs(L, rets)
    L"return "(nret)";"
    else
    L"return 0;"
    end
end

local function gen_stack_dispatcher(L, funclist)
    --[[]
    local lens = {}
    local lenlist = {}
    local optional = {}
    for i, v in ipairs(funclist) do
        if v.args.opt_idx then
            table.insert(optional, v)
        else
            local arglen = #v.args
            if not lens[arglen] then
                lens[arglen] = {}
                lenlist[#lenlist + 1] = arglen
            end
            table.insert(lens[arglen], v)
        end
    end
    for k, v in pairs(lens) do
        lens[k] = get_arg_categories(lens[k])
    end
    ]]
    
    local gettop = "lua_gettop(L)"
    --if optional[1] then
    L"int top = "(gettop)";"
        gettop = "top"
        local minlen
        for i, v in ipairs(funclist) do
            local arglen = 0
            for i, v in ipairs(v.args) do
                if not v:isopt() then arglen = arglen + 1 end
            end
            if arglen ~= 0 and (not minlen or arglen < minlen) then
                minlen = arglen
            end
        end
        local plist = get_arg_categories(funclist)
        plist.cond = "top >= "..minlen
        print_tree(plist)
        gen_conditions(L, plist, 1, gettop)
        do return end
    --end
    if #lenlist == 1 then
        assert(lenlist[1] ~= 0)
        lens[lenlist[1]].n = lenlist[1]
        gen_conditions(L, lens[lenlist[1]], 1, gettop)
    else
    L"switch ("(gettop)") {"
        table.sort(lenlist)
        for i, k in ipairs(lenlist) do
            local v = lens[k]
    L"case "(k)":"
            if k == 0 then
                assert(v.endnode)
    L"    return "(v.endnode.binder_name)"(L);"
            else
                v.n = nil
                gen_conditions(L(4), v, 1, gettop) L(-4)
    L"    break;"
            end
        end
    --L"default: break;"
    L"}"
    end
end

local gen_dispatcher_conditions = gen_simple_dispatcher

return function (name)
    local t = {}
    local cname
    local prefix
    local funclist = {}
    local curfunc
    local typelists = {}

    function t:prefix(p)
        prefix = p
        return self
    end

    function t:cname(cn)
        if not curfunc then
            cname = cn
        else
            curfunc.cname = cn
        end
        return self
    end

    function t:args(...)
        curfunc = {}
        funclist[#funclist+1] = curfunc
        local args = {n = select('#', ...), ...}
        local typelist = "(void)"
        if #args == 1 then
            args[1] = args[1]:ref "arg"
        end
        local opt_idx
        for i = 1, args.n do local v = args[i]
            if not v then
                error("argument #"..i.." is nil value", 2)
            end
            v = v:ref("arg", i); args[i] = v
            if opt_idx and not v:isopt() then
                error("argument #"..i..
                    " is not optional after argument #"..opt_idx, 2)
            end
            if v:isopt() and not opt_idx then
                opt_idx = i
            end
        end

        local names = {}
        if #args ~= 0 then
            typelist = {}
            local opt_typelist = {}
            for i, v in ipairs(args) do
                -- argument should have different name
                local argname = v:name()
                if names[argname] then
                    error('function "'..name..'" already have a argument '..
                        "(#"..names[argname]:idx()..") with name "..
                        '"'..argname..'"', 2)
                end
                names[argname] = v

                -- generate signature for function
                local s = v:ltype()
                opt_typelist[#typelist + 1] = s
                if opt_idx and i >= opt_idx-1 and i ~= args.n then
                    local sign = "("..table.concat(opt_typelist, ", ")..")"
                    if typelists[sign] then
                        error('function "'..name..'" '..
                            'already have a overload "has signature '..
                            'against optional arguments:\n    '..sign, 2)
                    end
                    typelists[sign] = true
                    s = s .. "["
                end
                typelist[#typelist + 1] = s
            end
            typelist = "("..table.concat(typelist, ", ")
            if opt_idx then
                typelist = typelist .. ("]"):rep(args.n - opt_idx + 1)
            end
            typelist = typelist..")"
        end
        args.nametable = names
        args.opt_idx = opt_idx

        curfunc.args, curfunc.typelist = args, typelist
        if typelists[typelist] then
            error("function \""..name.."\" already have a overload "..
                "has signature:\n    "..typelist, 2)
        end
        typelists[typelist] = true
        return self
    end

    function t:ret(...)
        local rets = {n = select('#', ...), ...}
        if #rets == 1 then
            rets[1] = rets[1]:ref "ret"
        end
        local names = curfunc.args.nametable
        for i = 1, rets.n do local v = rets[i]
            if not v then
                error("return value #"..i.." is nil value", 2)
            end
            v = v:ref("ret", i); rets[i] = v
            local retname = v:name()
            if names[retname] and v:ltype() ~= names[retname]:ltype() then
                error("return value #"..i.." has same name (\""..
                    retname.."\") with argument #"..names[retname]:idx()..
                    " but has different type", 2)
            end
        end
        curfunc.ret = rets
        return self
    end

    function t:body(nret)
        return function(s)
            local body = utils.getlines(s)
            body[#body + 1] = "return "..nret..";"
            curfunc.body = body
            return self
        end
    end

    local function collect_text(self, field)
        self[field] = function(self, ...)
            curfunc[field] = utils.getlines(table.concat {...})
            return self
        end
    end
    collect_text(t, 'pre')
    collect_text(t, 'post')
    collect_text(t, 'callline')

    -- generators

    local function prefixname()
        return (prefix and prefix.."_" or "")..name
    end

    local function get_binder_name(idx)
        return "lb_binder_"..prefixname().."_"..idx
    end

    function t:gen_register()
        return ("{ \"%s\", lb_dispatcher_%s }"):format(name, prefixname())
    end

    function t:gen_dispatcher()
        local funcbody = {}
        local L = utils.builder(funcbody)

        L"static int lb_dispatcher_"(prefixname())
            "(lua_State *L) {"
        if #funclist == 1 then
            -- check arguments
            L(4)
            gen_binder_body(L, cname, funclist[1],
                typeinfo.gen_checkargs(L, 1, func.args))
            L(-4)
        else
            for i, v in ipairs(funclist) do
                v.binder_name = get_binder_name(i)
            end
            gen_dispatcher_conditions(L(4), funclist) L(-4)
        L"    return lbind_matcherror(L, "
            for i, v in ipairs(funclist) do
        L"        \"    "(v.typelist)"\\n\""
            end
        L"    );"
        end
        L"}"

        return funcbody
    end

    function t:gen_binders()
        if #funclist == 1 then return {} end

        local funcbody = {}
        local L = utils.builder(funcbody)

        for i, func in ipairs(funclist) do
        L"static int "(get_binder_name(i))"(lua_State *L) {"
            -- get arguments
            L(4)
            gen_binder_body(L, cname, func,
                typeinfo.gen_getargs(L, 1, func.args))
            L(-4)
        L"}\n\n"
        end

        return funcbody
    end

    return t
end
