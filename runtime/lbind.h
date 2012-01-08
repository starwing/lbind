#ifndef LBIND_H
#define LBIND_H


#include <lua.h>
#include <lauxlib.h>


#define LBLIB_API LUALIB_API


/* compatible apis */
#if LUA_VERSION_NUM < 502
#  define LUA_OK                        0
#  define lua_getuservalue              lua_getfenv
#  define lua_setuservalue              lua_setfenv
#  define luaL_setfuncs(L,l,nups)       luaI_openlib((L),NULL,(l),(nups))
#  define luaL_newlibtable(L,l)	\
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)
#  define luaL_newlib(L,l) \
    (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

LBLIB_API void lua_rawgetp (lua_State *L, int narg, const void *p);
LBLIB_API void lua_rawsetp (lua_State *L, int narg, const void *p);

#endif /* LUA_VERSION_NUM < 502 */


/* lbind runtime */

LUALIB_API int lbind_getinfo (lua_State *L);
LUALIB_API int luaopen_lbind (lua_State *L);

/* lbind error process */

LBLIB_API int lbind_typeerror  (lua_State *L, int narg, const char *tname);
LBLIB_API int lbind_matcherror (lua_State *L, const char *extramsg);

/* lbind global informations */

LBLIB_API void lbind_getpointertable (lua_State *L);
LBLIB_API void lbind_gettypemaptable (lua_State *L);
LBLIB_API void lbind_getlibmaptable  (lua_State *L);
LBLIB_API void lbind_getlibmeta      (lua_State *L);
LBLIB_API void lbind_getenummeta     (lua_State *L);

/* lbind pointer registry */

LBLIB_API void lbG_register   (lua_State *L, int ud);
LBLIB_API void lbG_unregister (lua_State *L, int ud);
LBLIB_API int  lbG_shouldgc   (lua_State *L, int ud);

/* lbind class runtime */

/*
 * NOTE: all types must have a global Type structure. its address used
 * to find the right informations about type. So all const lbC_Type *t
 * argument must be &var, where var is declared by LBLIB_API, e.g.
 * LBLIB_API lbC_Type basetype;
 */
typedef struct lbC_Type lbC_Type;

typedef int (*lbC_testfunc)(lua_State *L, int narg, const lbC_Type *to_type);
typedef void *(*lbC_castfunc)(lua_State *L, int narg, const lbC_Type *to_type);

struct lbC_Type {
    const char *tname;
    int init_flags;
    lbC_testfunc testfunc;
    lbC_castfunc castfunc;
    lbC_Type **bases;
};

/* lbind type registry */
LBLIB_API void lbC_inittype    (lua_State *L, const char *tname, lbC_Type **bases, lbC_Type *t);
LBLIB_API void lbC_setmt       (lua_State *L, luaL_Reg *funcs, luaL_Reg *mts, lbC_Type *t);
LBLIB_API void lbC_setcast     (lua_State *L, lbC_testfunc tf, lbC_castfunc cf, lbC_Type *t);
LBLIB_API void lbC_setaccessor (lua_State *L, luaL_Reg *getters, luaL_Reg *setters, lbC_Type *t);

LBLIB_API void lbC_getmetatable (lua_State *L, const void *t);

#define lbC_nobase NULL
LBLIB_API luaL_Reg lbC_nomt[1];

#define lbC_newclass(L,name,funcs,base,mt,t) \
    ( lbC_inittype((L),(name),(base),(t)), \
      lua_createtable((L), 0, sizeof(funcs)/sizeof((funcs)[0])), \
      lua_createtable((L), 0, sizeof(mt)/sizeof((mt)[0]) + 3), \
      lbC_setmt((L),(funcs),(mt),(t)) )


/* lbind type system */
LBLIB_API const char *lbC_type      (lua_State *L, int ud);
LBLIB_API int         lbC_isa       (lua_State *L, int ud, const lbC_Type *t);
LBLIB_API void       *lbC_cast      (lua_State *L, int ud, const lbC_Type *t);

/* lbind object maintain */
LBLIB_API void  lbO_register    (lua_State *L, const void *p, const lbC_Type *t);
LBLIB_API void *lbO_unregister  (lua_State *L, int ud);
LBLIB_API void  lbO_copyobject  (lua_State *L, const void *p, const lbC_Type *t);
LBLIB_API void *lbO_isobject    (lua_State *L, int ud, const lbC_Type *t);
LBLIB_API void *lbO_checkobject (lua_State *L, int ud, const lbC_Type *t);
LBLIB_API void *lbO_toobject    (lua_State *L, int ud);


/* lbind metatable routine */

#define lbM_basetable   "__bases"
#define lbM_libtable    "__methods"
#define lbM_typeinfo    "__typeinfo"
#define lbM_gettertable "__get"
#define lbM_settertable "__set"

#define lbM_getfield(L,f) lua_getfield(L,-1,f)
#define lbM_setfield(L,f) lua_setfield(L,-2,f)

/* lbind enum runtime */

typedef struct {
    const char *name;
    int value;
} lbE_Enum;

typedef struct {
    const char *name;
    lbE_Enum *enums;
} lbE_EnumType;

LBLIB_API void lbE_initenum (lua_State *L, const char *name, lbE_Enum *enums, lbE_EnumType *et);

LBLIB_API int  lbE_isenum    (lua_State *L, int narg, lbE_EnumType *et);
LBLIB_API void lbE_pushenum  (lua_State *L, int evalue, lbE_EnumType *et);
LBLIB_API int  lbE_toenum    (lua_State *L, int narg, lbE_EnumType *et);
LBLIB_API int  lbE_checkenum (lua_State *L, int narg, lbE_EnumType *et);

#define lbC_newenum(L,name,enums,et) \
    ( luaL_newlibtable((L), (enums)), \
      lbE_initenum((L), (name), (enums), (et)) )

/* lbind template runtime */

LBLIB_API int lbT_newtemplate (lua_State *L, luaL_Reg *temps, const char *tname);


#endif /* LBIND_H */
