#define LUA_LIB
#include "lbind.h"


/* lbind error maintain */

int lbind_typeerror(lua_State *L, int narg, const char *tname)
{
    const char *real_type = lbC_type(L, narg);
    const char *msg;
    if (real_type == NULL)
        real_type = luaL_typename(L, narg);
    msg = lua_pushfstring(L, "%s expected, got %s",
                          tname, real_type);
    return luaL_argerror(L, narg, msg);
}

int lbind_matcherror(lua_State *L, const char *extramsg)
{
    lua_Debug ar;
    lua_getinfo(L, "n", &ar);
    if (ar.name == NULL)
        ar.name = "?";
    return luaL_error(L, "no matching functions for call to %s\n"
                      "candidates are:\n%s", ar.name, extramsg);
}

/* lbind Lua side runtime */

static int lbR_register(lua_State *L)
{
    lbG_register(L, -1);
    return 1;
}

static int lbR_unregister(lua_State *L)
{
    lbG_unregister(L, -1);
    return 1;
}

static int lbR_owner(lua_State *L)
{
    int i, top = lua_gettop(L);
    luaL_checkstack(L, top, "no space for owner info");
    for (i = 1; i <= top; ++i) {
        if (lbG_shouldgc(L, i))
            lua_pushliteral(L, "Lua");
        else
            lua_pushliteral(L, "C");
    }
    return top;
}

static int lbR_null(lua_State *L)
{
    const void *u = lbO_toobject(L, -1);
    if (u != NULL)
        lua_pushlightuserdata(L, (void*)u);
    else
        lua_pushnil(L);
    return 1;
}

static int lbR_valid(lua_State *L)
{
    const void *u = lua_touserdata(L, -1);
    if (u != NULL) {
        lbind_getpointertable(L);
        lua_rawgetp(L, -1, u);
    }
    else
        lua_pushnil(L);
    return 1;
}

static int lbR_delete(lua_State *L)
{
    lua_getfield(L, -1, "delete");
    if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, -2);
        lua_call(L, 1, 0);
    }
    return 0;
}

static int is_type(lua_State *L, const void *t, int ch)
{
    int istype = 0;
    lua_rawgetp(L, LUA_REGISTRYINDEX, t);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_rawgetp(L, -1, t);
    istype = lua_isnil(L, -1) || *lua_tostring(L, -1) != ch;
    lua_pop(L, 1);
    return istype; /* remain table on top */
}

static void get_typeptr(lua_State *L)
{
    if (!lua_islightuserdata(L, -1)) {
        if (lua_isstring(L, -2))
            lbind_gettypemaptable(L); /* 1 */
        else
            lbind_getlibmaptable(L); /* 1 */
        lua_pushvalue(L, -2); /* 2 */
        lua_rawget(L, -2); /* 2->2 */
        lua_remove(L, -2); /* (1) */
    }
}

static int lbR_isa(lua_State *L)
{
    get_typeptr(L);
    if (lua_islightuserdata(L, -1)) {
        const lbC_Type *t = (const lbC_Type*)lua_touserdata(L, -1);
        if (is_type(L, t, 'c')) {
            lua_pushboolean(L, lbC_isa(L, -3, t));
            return 1;
        }
    }
    return 0;
}

static int lbR_cast(lua_State *L)
{
    get_typeptr(L);
    if (lua_islightuserdata(L, -1)) {
        const lbC_Type *t = (const lbC_Type*)lua_touserdata(L, -1);
        void *u;
        if (is_type(L, t, 'c') && (u = lbC_cast(L, -3, t)) != NULL) {
            lbO_register(L, u, t);
            return 1;
        }
    }
    return 0;
}

static int get_libmeta(lua_State *L)
{
    lbind_getlibmaptable(L); /* 1 */
    lua_pushvalue(L, -2); /* 2 */
    lua_rawget(L, -2); /* 2->2 */
    lua_remove(L, -2); /* (1) */
    if (lua_islightuserdata(L, -1))
        return 1;
    lua_pop(L, 1); /* (1) */
    return 0;
}

static int lbR_type(lua_State *L)
{
    const char *type = NULL;
    if (lua_isuserdata(L, -1))
        type = lbC_type(L, -1);
    else if (lua_islightuserdata(L, -1)
             || (lua_istable(L, -1) && get_libmeta(L))) {
        const char *tag;
        lbC_Type *t;
        t = (lbC_Type*)lua_touserdata(L, -1);
        lua_rawgetp(L, LUA_REGISTRYINDEX, t);
        lua_rawgetp(L, -1, t);
        tag = lua_tostring(L, -1);
        lua_pop(L, 2);
        if (tag != NULL && (*tag == 'c' || *tag == 'e'))
            type = t->tname;
    }
    lua_pushstring(L, type != NULL ? type : lua_typename(L, -1));
    return 1;
}

static int get_classmt(lua_State *L)
{
    return ((lua_islightuserdata(L, -1)
             && is_type(L, lua_touserdata(L, -1), 'c'))
            || (lua_istable(L, -1) && get_libmeta(L))
            || (lua_getmetatable(L, -1)));
}

static int lbR_methods(lua_State *L)
{
    if (!get_classmt(L)) return 0;
    lbM_getfield(L, lbM_libtable);
    return 1;
}

static int lbR_bases(lua_State *L)
{
    lbC_Type *t;
    int incomplete = 0;
    if (!get_classmt(L)) return 0;
    lbM_getfield(L, lbM_basetable); /* 2 */
    if (!lua_isnil(L, -1)) {
        lua_pushliteral(L, "incomplete"); /* 3 */
        lua_rawget(L, -2); /* 3->3 */
        incomplete = lua_toboolean(L, -1);
        lua_pop(L, 1); /* (3) */
        if (!incomplete)
            return 1;
    }
    lbM_getfield(L, lbM_typeinfo); /* 3 */
    if ((t = (lbC_Type*)lua_touserdata(L, -1)) != NULL) {
        int i;
        lbC_Type **bi;
        lua_pop(L, 1); /* (3) */
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1); /* (2) */
            lua_newtable(L); /* 2 */
            lua_newtable(L); /* 3 */
            lua_pushliteral(L, "__mode"); /* 4 */
            lua_pushliteral(L, "v"); /* 5 */
            lua_rawset(L, -3); /* 4,5->3 */
            lua_setmetatable(L, -2); /* 3->2 */
        }
        if (t->bases != NULL) {
            for (i = 1, bi = t->bases; *bi != NULL; ++bi, ++i) {
                lbC_getmetatable(L, *bi); /* 3 */
                if (lua_isnil(L, -1)) {
                    lua_pushlightuserdata(L, *bi); /* 4 */
                    incomplete = 1;
                }
                else
                    lbM_getfield(L, lbM_libtable); /* 4 */
                lua_remove(L, -2); /* (3) */
                lua_rawseti(L, -2, i); /* 3->2 */
            }
        }
        if (incomplete) {
            lua_pushliteral(L, "incomplete"); /* 3 */
            lua_pushboolean(L, 1); /* 4 */
            lua_rawset(L, -3); /* 3,4->2 */
        }
        lua_pushvalue(L, -2); /* 1->3 */
        lua_pushvalue(L, -2); /* 2->4 */
        lbM_setfield(L, lbM_basetable); /* 4->3 */
        lua_pop(L, 1); /* (3) */
        return 1;
    }
    return 0;
}

static int lbR_enum(lua_State *L)
{
    get_typeptr(L);
    if (lua_islightuserdata(L, -1)) {
        int value, first = 1;
        luaL_Buffer b;
        lbE_Enum *ei;
        lbE_EnumType *et = (lbE_EnumType*)lua_touserdata(L, -1);
        if (!is_type(L, et, 'e'))
            return 0;
        lua_pop(L, 1); /* pop enum table from is_type */
        value = lbE_checkenum(L, -2, et);
        luaL_buffinit(L, &b);
        for (ei = et->enums; ei->name != NULL; ++ei) {
            if ((ei->value & value) == value) {
                if (first)
                    first = 0;
                else
                    luaL_addstring(&b, ", ");
                luaL_addstring(&b, ei->name);
                value &= ~ei->value;
            }
        }
        luaL_pushresult(&b);
        return 1;
    }
    return 0;
}

static luaL_Reg lbind_funcs[] = {
    { "bases",      lbR_bases      },
    { "cast",       lbR_cast       },
    { "delete",     lbR_delete     },
    { "enum",       lbR_enum       },
    { "info",       lbind_getinfo  },
    { "isa",        lbR_isa        },
    { "methods",    lbR_methods    },
    { "null",       lbR_null       },
    { "owner",      lbR_owner      },
    { "register",   lbR_register   },
    { "type",       lbR_type       },
    { "unregister", lbR_unregister },
    { "valid",      lbR_valid      },
    { NULL, NULL },
};

int luaopen_lbind(lua_State *L)
{
    luaL_newlib(L, lbind_funcs);
#if LUA_VERSION_NUM < 502
    lua_pushvalue(L, -1);
    lua_setglobal(L, "lbind");
#endif
    return 1;
}

/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-DLUA_BUILD_AS_DLL' input='lb*.c' output='lbind.dll'
 * cc: run='$lua tt.lua'
 */
