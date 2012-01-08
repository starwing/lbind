#define LUA_LIB
#include "lbind.h"

#include <ctype.h>

/* parse string enumerate value */

/*#define skip_white(s) ((s) += strspn((s), " \t\r\n,|"))*/
#define skip_white(s) do { \
        while (*(s) == ' ' || *(s) == '\t' || *(s) == '\r' \
                || *(s) == '\n' || *(s) == ',' || *(s) == '|' \
              ) ++(s); } while (0)

static int parse_ident(lua_State *L, const char *p, int *value)
{
    if (isalpha(*p) || *p == '_') {
        int valid;
        const char *begin = p++;
        while (isalnum(*p) || *p == '_')
            ++p;
        lua_pushlstring(L, begin, p - begin);
        lua_rawget(L, -2);
        valid = (*value = lua_tonumber(L, -1)) != 0 || lua_isnumber(L, -1);
        lua_pop(L, 1);
        if (!valid) {
            lua_pushlstring(L, begin, p - begin);
            lua_pushfstring(L, "unexpected ident " LUA_QS " in enum",
                            lua_tostring(L, -1));
            lua_remove(L, -2);
            return 0;
        }
        return 1;
    }
    else {
        char ch[2] = {0};
        ch[0] = *p;
        lua_pushfstring(L, "unexpected token " LUA_QS " in enum", ch);
        return 0;
    }
}

static int parse_enum(lua_State *L, const char *s, int *penum, int check)
{
    *penum = 0;
    while (*s != '\0') {
        int evalue;
        int inversion = 0;
        skip_white(s);
        if (*s == '~') {
            ++s;
            inversion = 1;
            skip_white(s);
        }
        if (*s != '\0') {
            if (!parse_ident(L, s, &evalue)) {
                if (!check) {
                    lua_pop(L, 1);
                    return 0;
                }
                else return lua_error(L);
            }
            if (inversion)
                *penum &= ~evalue;
            else
                *penum |= evalue;
        }
    }
    return 1;
}

/* lbind enum maintain */

int lbE_isenum(lua_State *L, int narg, lbE_EnumType *et)
{
    return lua_isnumber(L, narg) || lua_isstring(L, narg);
}

void lbE_pushenum(lua_State *L, int evalue, lbE_EnumType *et)
{
    lua_pushinteger(L, evalue);
}

static int toenum(lua_State *L, int narg, lbE_EnumType *et, int check)
{
    const char *str;
    int value = lua_tointeger(L, narg);
    if (value != 0 || lua_isnumber(L, narg))
        return value;
    if ((str = lua_tostring(L, narg)) != NULL) {
        int success;
        lua_rawgetp(L, LUA_REGISTRYINDEX, et);
        success = parse_enum(L, str, &value, check);
        lua_pop(L, 1);
        if (success)
            return value;
    }
    if (check)
        lbind_typeerror(L, narg, et->name);
    return 0;
}

int lbE_toenum(lua_State *L, int narg, lbE_EnumType *et)
{
    return toenum(L, narg, et, 0);
}

int lbE_checkenum(lua_State *L, int narg, lbE_EnumType *et)
{
    return toenum(L, narg, et, 1);
}

void lbE_initenum(lua_State *L, const char *name, lbE_Enum *enums, lbE_EnumType *et)
{
    /* stack: etable */
    et->name = name;
    et->enums = enums;

    for (; enums->name != NULL; ++enums) {
        lua_pushstring(L, enums->name); /* 1 */
        lua_pushinteger(L, enums->value); /* 2 */
        lua_rawset(L, -3); /* 1,2->etable */
    }

    /* set metatable */
    lbind_getenummeta(L);
    lua_setmetatable(L, -2);

    /* set typeinfo for enum table */
    lua_pushlightuserdata(L, et); /* 1 */
    lua_pushstring(L, "enum"); /* 2 */
    lua_rawset(L, -3); /* 1,2->etable */

    /* set global informations */
    /* see lbC_setmt in lb_class.c for details */
    lbind_getlibmaptable(L); /* 1 */
    lua_pushvalue(L, -4); /* etable->2 */
    lua_pushlightuserdata(L, et); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 1); /* (1) */
    lbind_gettypemaptable(L); /* 1 */
    lua_pushstring(L, name); /* 2 */
    lua_pushlightuserdata(L, et); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 1); /* (1) */

    /* set enum table to registry */
    lua_pushvalue(L, -1); /* 1 */
    lua_rawsetp(L, LUA_REGISTRYINDEX, et); /* 1->env */
}

/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-DLUA_BUILD_AS_DLL' input='lb*.c' output='lbind.dll'
 * cc: run='$lua tt.lua'
 */
