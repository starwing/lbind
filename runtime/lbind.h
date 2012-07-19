#ifndef LBIND_H
#define LBIND_H


#include <lua.h>
#include <lauxlib.h>

#define LB_API      LUA_API
#define LBLIB_API   LUALIB_API

#if LUA_VERSION_NUM < 502
#  define luaL_newlibtable(L,l)	\
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)
#  define luaL_newlib(L,l) \
    (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

LUALIB_API void luaL_setfuncs (lua_State *L, luaL_Reg *l, int nup);

#endif /* LUA_VERSION_NUM */


/* lbind runtime */

LUALIB_API int luaopen_lbind (lua_State *L);


/* lbind internal max alignment */

#ifndef LBIND_MAXALIGN
# define LBIND_MAXALIGN union { double u; void *s; long l; }
#endif

typedef LBIND_MAXALIGN lbind_MaxAlign;


/* lbind class install */

typedef struct lbind_Reg {
    const char    *name; /* name of library */
    lua_CFunction  open_func; /* luaopen_ function of library */
} lbind_Reg;

void lbind_install (lua_State *L, lbind_Reg *reg);


/* lbind error process */

LB_API int lbind_typeerror  (lua_State *L, int idx, const char *tname);
LB_API int lbind_matcherror (lua_State *L, const char *extramsg);


/* lbind class runtime */

/*
 * NOTE: all types must have a global Type structure. its address used
 * to find the right informations about type. So all const lbind_Type *t
 * argument must be &var, where var is declared by LB_API, e.g.
 * LB_API lbind_Type basetype;
 */
typedef struct lbind_Type lbind_Type;

typedef void *lbind_Cast(lua_State *L, int idx, const lbind_Type *to_type);

struct lbind_Type {
    const char *name;
    int flags;
    lbind_Cast *cast;
    lbind_Type **bases;
};

/* lbind_Type flags */
LB_API int lbind_setautotrack (lua_State *L, int autotrack, lbind_Type *t);

/* lbind type registry */
LB_API void lbind_inittype    (lua_State *L, const char *name, lbind_Type **bases, lbind_Type *t);
LB_API void lbind_setmt       (lua_State *L, lbind_Type *t);
LB_API void lbind_setcast     (lua_State *L, lbind_Cast *cast, lbind_Type *t);
LB_API void lbind_setaccessor (lua_State *L, luaL_Reg *getters, luaL_Reg *setters, lbind_Type *t);

LB_API int lbind_getmetatable (lua_State *L, const lbind_Type *t);

#define lbind_newclass(L,name,funcs,base,t) \
    ( lbind_inittype((L),(name),(base),(t)), \
      luaL_newlib((L), funcs), \
      lua_createtable((L), 0, 4), \
      lbind_setmt((L), (t)) )

#define lbind_newclass_meta(L,name,funcs,base,t) \
    ( lbind_inittype((L),(name),(base),(t)), \
      luaL_newlib((L), funcs), \
      luaL_newlib((L), funcs##_meta), \
      lbind_setmt((L), (t)) )

/* lbind type system */
LB_API const char *lbind_tolstring (lua_State *L, int idx, size_t *plen);
LB_API const char *lbind_type      (lua_State *L, int idx);

LB_API int   lbind_isa   (lua_State *L, int idx, const lbind_Type *t);
LB_API void  lbind_copy  (lua_State *L, const void *p, const lbind_Type *t);
LB_API void *lbind_cast  (lua_State *L, int idx, const lbind_Type *t);
LB_API void *lbind_check (lua_State *L, int idx, const lbind_Type *t);
LB_API void *lbind_test  (lua_State *L, int idx, const lbind_Type *t);

#define lbind_tostring(L,idx) lbind_tolstring((L),(idx),NULL)

/* lbind object maintain */
LB_API void *lbind_raw      (lua_State *L, size_t objsize);
LB_API void *lbind_object   (lua_State *L, int idx);
LB_API int   lbind_retrieve (lua_State *L, const void *p);

LB_API void *lbind_new        (lua_State *L, size_t objsize, const lbind_Type *t);
LB_API void  lbind_register   (lua_State *L, const void *p, const lbind_Type *t);
LB_API void *lbind_unregister (lua_State *L, int idx);

#define lbind_optobject(L,idx,defs,t) \
    (lua_isnoneornil((L),(idx)) ? (defs) : lbind_check((L),(idx),(t)))

/* lbind pointer registry */
LB_API void lbind_track    (lua_State *L, int idx);
LB_API void lbind_untrack  (lua_State *L, int idx);
LB_API int  lbind_hastrack (lua_State *L, int idx);


/* lbind enum runtime */

typedef struct lbind_EnumItem {
    const char *name;
    int value;
} lbind_EnumItem;

typedef struct lbind_Enum {
    const char *name;
    int lastn;
    lbind_EnumItem *enums;
} lbind_Enum;


LB_API void lbind_initenum  (lua_State *L, const char *name, lbind_EnumItem *enums, lbind_Enum *et);
LB_API int  lbind_addenum   (lua_State *L, int idx, lbind_Enum *et);
LB_API int  lbind_addenums  (lua_State *L, lbind_EnumItem *enums, lbind_Enum *et);

LB_API int lbind_pushenum  (lua_State *L, const char *name, lbind_Enum *et);
LB_API int lbind_testenum  (lua_State *L, int idx, lbind_Enum *et);
LB_API int lbind_checkenum (lua_State *L, int idx, lbind_Enum *et);

LB_API int lbind_pushmask  (lua_State *L, int evalue, lbind_Enum *et);
LB_API int lbind_testmask  (lua_State *L, int idx, lbind_Enum *et);
LB_API int lbind_checkmask (lua_State *L, int idx, lbind_Enum *et);

LB_API int lbind_getenumtable (lua_State *L, const lbind_Enum *et);

#define lbind_optenum(L,idx,defs,t) \
    (lua_isnoneornil((L),(idx)) ? (defs) : lbind_checkenum((L),(idx),(t)))

#define lbind_newenum(L,name,enums,et) \
    ( luaL_newlibtable((L), (enums)), \
      lbind_initenum((L), (name), (enums), (et)) )


#endif /* LBIND_H */
