#define LUA_LIB
#include "lbind.h"


#include <assert.h>
#include <ctype.h>


/* compatible apis */
#if LUA_VERSION_NUM < 502
#  define LUA_OK                        0
#  define lua_getuservalue              lua_getfenv
#  define lua_setuservalue              lua_setfenv
#  define lua_rawlen                    lua_objlen
#  define luaL_setfuncs(L,l,nups)       luaI_openlib((L),NULL,(l),(nups))
#  define luaL_newlibtable(L,l)	\
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)
#  define luaL_newlib(L,l) \
    (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

static void lua_rawgetp(lua_State *L, int narg, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_rawget(L, narg < 0 ? narg - 1 : narg);
}

static void lua_rawsetp(lua_State *L, int narg, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_insert(L, -2);
    lua_rawset(L, narg < 0 ? narg - 1 : narg);
}

static int lua_absindex(lua_State *L, int idx) {
    return (idx > 0 || idx <= LUA_REGISTRYINDEX)
           ? idx
           : lua_gettop(L) + idx + 1;
}

static const char *luaL_tolstring(lua_State *L, int idx, size_t *plen) {
  if (!luaL_callmeta(L, idx, "__tostring")) {  /* no metafield? */
    switch (lua_type(L, idx)) {
      case LUA_TNUMBER:
      case LUA_TSTRING:
        lua_pushvalue(L, idx);
        break;
      case LUA_TBOOLEAN:
        lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
        break;
      case LUA_TNIL:
        lua_pushliteral(L, "nil");
        break;
      default:
        lua_pushfstring(L, "%s: %p", luaL_typename(L, idx),
                                            lua_topointer(L, idx));
        break;
    }
  }
  return lua_tolstring(L, -1, len);
}

#endif /* LUA_VERSION_NUM < 502 */


/* lbind class register */

void lbind_register(lua_State *L, lb_Reg *reg) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD");
    for (; reg->name != NULL; ++reg) {
        lua_pushstring(L, reg->name);
        lua_pushcfunction(L, reg->open_func);
        lua_rawset(L, -3);
    }
    lua_pop(L, 1);
}


/* lbind error maintain */

int lbind_typeerror(lua_State *L, int narg, const char *tname) {
    const char *real_type = lbC_type(L, narg);
    const char *msg;
    if (real_type == NULL)
        real_type = luaL_typename(L, narg);
    msg = lua_pushfstring(L, "%s expected, got %s",
                          tname, real_type);
    return luaL_argerror(L, narg, msg);
}

int lbind_matcherror(lua_State *L, const char *extramsg) {
    lua_Debug ar;
    lua_getinfo(L, "n", &ar);
    if (ar.name == NULL)
        ar.name = "?";
    return luaL_error(L, "no matching functions for call to %s\n"
                      "candidates are:\n%s", ar.name, extramsg);
}


/* lbind global informations */

/* algorithm (perl): "0xFB" . (unpack "H*", pack "H*", $str) */
#define lbind_ptrbox    ((void*)0xFB9DBB81)  /* ptrbox */
#define lbind_typebox   ((void*)0xFBD2927F)  /* typinf */
#define lbind_libbox    ((void*)0xFB52BB81)  /* libbox */
#define lbind_libmeta   ((void*)0xFB52B6E1)  /* libmex */
#define lbind_enummeta  ((void*)0xFBE7E6E1)  /* enumex */

static void setnewtable(lua_State *L, const void *p, const char *config) {
    lua_newtable(L); /* 1 */
    if (config != NULL) {
        lua_newtable(L); /* 2 */
        lua_pushliteral(L, "__mode"); /* 3 */
        lua_pushstring(L, config); /* 4 */
        lua_rawset(L, -3); /* 3,4->2 */
        lua_setmetatable(L, -2); /* 2->1 */
    }
    lua_pushvalue(L, -1); /* 1->2 */
    lua_rawsetp(L, LUA_REGISTRYINDEX, p); /* 2->env */
}

static void get_pointertable(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, lbind_ptrbox);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        setnewtable(L, lbind_ptrbox, "v");
    }
}

static void get_typemap(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, lbind_typebox);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        setnewtable(L, lbind_typebox, "v");
    }
}

static void get_libmap(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, lbind_libbox);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        setnewtable(L, lbind_libbox, "v");
    }
}

static int libcall_helper(lua_State *L) {
    lua_getfield(L, 1, "new_local");
    if (lua_isnil(L, -1))
        return luaL_argerror(
                   L, 1, "no new_local function found");
    lua_replace(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

static void get_libmeta(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, lbind_libmeta);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        setnewtable(L, lbind_libmeta, NULL);
        lua_pushliteral(L, "__call"); /* 2 */
        lua_pushcfunction(L, libcall_helper); /* 3 */
        lua_rawset(L, -3); /* 2,3->1 */
    }
}

static int enumcall_helper(lua_State *L) {
    lbE_EnumType *et;
    get_libmeta(L); /* 1 */
    lua_pushvalue(L, 1); /* 2 */
    lua_rawget(L, -2); /* 2->2 */
    if ((et = (lbE_EnumType*)lua_touserdata(L, -1)) == NULL)
        return 0;
    lua_pushinteger(L, lbE_checkenum(L, 2, et));
    return 1;
}

static void get_enummeta(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, lbind_enummeta);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        setnewtable(L, lbind_enummeta, NULL);
        lua_pushliteral(L, "__call"); /* 2 */
        lua_pushcfunction(L, enumcall_helper); /* 3 */
        lua_rawset(L, -3); /* 2,3->1 */
    }
}


/* lbind userdata maintain */

typedef struct {
    void *instance;
    int flags;
} lbC_Object;

#define LBC_GC          0x01
#define LBC_HASSETTER   0x02
#define LBC_HASGETTER   0x04

#define check_size(L,n) (lua_rawlen((L),(n)) == sizeof(lbC_Object))


/* lbind memory maintain */

static lbC_Object *to_object(lua_State *L, int ud) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (obj != NULL) {
        if (!check_size(L, ud) || obj->instance == NULL)
            obj = NULL;
        else {
            ud = lua_absindex(L, ud);
            get_pointertable(L); /* 1 */
            lua_rawgetp(L, -1, obj->instance); /* 2 */
            if (!lua_rawequal(L, ud, -1))
                obj = NULL;
            lua_pop(L, 2); /* (2)(1) */
        }
    }
    return obj;
}

void lbG_register(lua_State *L, int narg) {
    lbC_Object *obj = to_object(L, narg);
    if (obj != NULL)
        obj->flags |= LBC_GC;
}

void lbG_unregister(lua_State *L, int narg) {
    lbC_Object *obj = to_object(L, narg);
    if (obj != NULL)
        obj->flags &= ~LBC_GC;
}

int lbG_shouldgc(lua_State *L, int narg) {
    lbC_Object *obj = to_object(L, narg);
    return obj != NULL && (obj->flags & LBC_GC) != 0;
}


/* lbind class maintain */

#define lbM_basetable   "__bases"
#define lbM_libtable    "__methods"
#define lbM_typeinfo    "__typeinfo"
#define lbM_gettertable "__get"
#define lbM_settertable "__set"

#define lbM_getfield(L,f) lua_getfield(L,-1,f)
#define lbM_setfield(L,f) lua_setfield(L,-2,f)

void lbC_inittype(lua_State *L, const char *tname, lbC_Type **bases, lbC_Type *t) {
    t->tname = tname;
    t->bases = bases;
    t->init_flags = 0;
    t->castfunc = NULL;
}

static int newlocal_helper(lua_State *L) {
    lua_getfield(L, lua_upvalueindex(1), "new");
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    lbG_register(L, 1);
    return lua_gettop(L);
}

static int lbM_newindex(lua_State *L) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL
            && (obj->flags & LBC_HASSETTER) != 0
            && lua_getmetatable(L, 1)) { /* 1 */
        lua_CFunction setter;
        /* setter table */
        lbM_getfield(L, lbM_settertable); /* 2 */
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, 2); /* 3 */
            lua_rawget(L, -2); /* 3->3 */
            setter = lua_tocfunction(L, -1);
            lua_pop(L, 3);
            if (setter != NULL)
                return setter(L);
        }
    }
    /* set it in uservalue table */
    if (lua_isuserdata(L, 1)) {
        lua_getuservalue(L, 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setuservalue(L, 1);
        }
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_rawset(L, -3);
    }
    return 0;
}

static int lbM_gc(lua_State *L) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL && check_size(L, 1)) {
        if ((obj->flags & LBC_GC) != 0) {
            lua_getfield(L, 1, "delete");
            if (!lua_isnil(L, -1)) {
                lua_pushvalue(L, 1);
                lua_call(L, 1, 0);
            }
            if ((obj->flags & LBC_GC) != 0)
                lbO_unregister(L, 1);
        }
    }
    return 0;
}

static int lbM_tostring(lua_State *L) {
    lbO_tolstring(L, 1, NULL);
    return 1;
}

static int lbM_index(lua_State *L) {
    int i;
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL
            && (obj->flags & LBC_HASGETTER) != 0
            && lua_getmetatable(L, 1)) { /* 1 */
        lua_CFunction getter;
        /* getter table */
        lbM_getfield(L, lbM_gettertable); /* 2 */
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, 2); /* 3 */
            lua_rawget(L, -2); /* 3->3 */
            getter = lua_tocfunction(L, -1);
            lua_pop(L, 3);
            if (getter != NULL)
                return getter(L);
        }
    }
    /* find in libtable/superlibtable */
    for (i = 1; !lua_isnone(L, lua_upvalueindex(i)); ++i) {
        if (lua_islightuserdata(L, lua_upvalueindex(i))) {
            lbC_getmetatable(L, (const lbC_Type*)lua_touserdata(L, lua_upvalueindex(i)));
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            lbM_getfield(L, lbM_libtable);
            lua_insert(L, lua_upvalueindex(i));
        }
        lua_pushvalue(L, 2);
        lua_rawget(L, lua_upvalueindex(i));
        if (!lua_isnil(L, -1))
            return 1;
    }
    /* find in uservalue table */
    if (lua_isuserdata(L, 1)) {
        lua_getuservalue(L, 1);
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, 2);
            lua_rawget(L, -2);
        }
        return 1; /* nil or value in uservalue table */
    }
    return 0;
}

static void push_indexfunc(lua_State *L, lbC_Type **bases) {
    int nups = 1; /* stack: libtable */
    if (bases != NULL) {
        for (; *bases != NULL; ++nups, ++bases) {
            lbC_getmetatable(L, *bases);
            if (lua_isnil(L, -1))
                lua_pushlightuserdata(L, *bases);
            else
                lbM_getfield(L, lbM_libtable);
            lua_remove(L, -2);
            luaL_checkstack(L, 1, "no space for base type");
        }
    }
    lua_pushcclosure(L, lbM_index, nups);
}

static void set_default(lua_State *L, int idx, const char *key) {
    lua_getfield(L, idx, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_setfield(L, idx, key);
    }
    else
        lua_pop(L, 2);
}

static void register_global_info(lua_State *L, const char *tname, void *t) {
    /* stack: libtable */

    /* sorry for complicate here, we must register informations to
     * below tables:
     *   - registry:     lbC_Type*->metatable
     *   - libmap:       libtable->lbC_Type*
     *   - typeinfo:     name->lbC_Type*
     *   ( metatable:    __methods->libtable
     *                   __typeinfo->lbC_Type*
     *     lbC_Type*:    tname->name )
     * this is for name/libtable/metatable/lbC_Type* transform.
     */
    get_typemap(L); /* 1 */
    lua_pushstring(L, tname); /* 2 */
    lua_pushlightuserdata(L, t); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 1); /* (1) */
    get_libmap(L); /* 1 */
    lua_pushvalue(L, -2); /* libtable->2 */
    lua_pushlightuserdata(L, t); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 2); /* (1)(libtable) */
}

void lbC_setmt(lua_State *L, lbC_Type *t) {
    /* stack: libtable mt */

    /* add new_local function */
    lua_getfield(L, -2, "new"); /* 1 */
    lua_getfield(L, -3, "new_local"); /* 2 */
    if (!lua_isnil(L, -2) && lua_isnil(L, -1)) {
        lua_pushvalue(L, -4); /* libtable->3 */
        lua_pushcclosure(L, newlocal_helper, 1); /* 3->3 */
        lua_setfield(L, -5, "new_local"); /* 3->libtable */
    }
    lua_pop(L, 2);

    /* set metatable to libtable */
    get_libmeta(L); /* 1 */
    lua_setmetatable(L, -3); /* 1->libtable */

    /* init type metatable */
    lua_pushvalue(L, -2); /* libtable->1 */
    push_indexfunc(L, t->bases); /* 1->1 */
    set_default(L, -2, "__index"); /* 1->mt */
    lua_pushcfunction(L, lbM_newindex); /* 1 */
    set_default(L, -2, "__newindex"); /* 1->mt */
    lua_pushcfunction(L, lbM_gc); /* 1 */
    set_default(L, -2, "__gc"); /* 1->mt */
    lua_pushcfunction(L, lbM_tostring); /* 1 */
    set_default(L, -2, "__tostring"); /* 1->mt */

    /* set typeinfo for metatable */
    lua_pushliteral(L, "class"); /* 1 */
    lua_rawsetp(L, -3, t); /* 1->mt */
    lua_pushvalue(L, -2); /* libtable->1 */
    lbM_setfield(L, lbM_libtable); /* 1->mt */
    lua_pushlightuserdata(L, (void*)t); /* 1 */
    lbM_setfield(L, lbM_typeinfo); /* 1->mt */

    /* set global informations */
    lua_pushvalue(L, -2); /* libtable->1 */
    register_global_info(L, t->tname, t); /* (1) */

    /* set metatable to registry */
    lua_rawsetp(L, LUA_REGISTRYINDEX, t); /* (mt) */
}

void lbC_setaccessor(lua_State *L, luaL_Reg *getters, luaL_Reg *setters, lbC_Type *t) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, t);
    if (getters) {
        lua_newtable(L);
        luaL_setfuncs(L, getters, 0);
        lbM_setfield(L, lbM_gettertable);
        t->init_flags |= LBC_HASGETTER;
    }
    if (setters) {
        lua_newtable(L);
        luaL_setfuncs(L, setters, 0);
        lbM_setfield(L, lbM_settertable);
        t->init_flags |= LBC_HASSETTER;
    }
    lua_pop(L, 1);
}

void lbC_setcast(lua_State *L, lbC_castfunc cf, lbC_Type *t) {
    t->castfunc = cf;
}

void lbC_getmetatable(lua_State *L, const void *t) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, t);
}


/* lbind type system */

static int testudata(lua_State *L, int ud, const void *t) {
    if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
        lbC_getmetatable(L, t); /* get correct metatable */
        if (!lua_rawequal(L, -1, -2))  /* not the same? */
            return 0;  /* value is a userdata with wrong metatable */
        lua_pop(L, 2);  /* remove both metatables */
        return 1;
    }
    return 0;
}

const char *lbC_type(lua_State *L, int narg) {
    lbC_Object *obj = to_object(L, narg);
    if (obj != NULL && lua_getmetatable(L, narg)) { /* 1 */
        const lbC_Type *ti = NULL;
        lbM_getfield(L, lbM_typeinfo); /* 2 */
        if (!lua_isnil(L, -1))
            ti = (const lbC_Type*)lua_touserdata(L, -1);
        lua_pop(L, 2); /* (2)(1) */
        if (ti != NULL)
            return ti->tname;
    }
    return NULL;
}

void *try_cast(lua_State *L, int narg, const lbC_Type *t) {
    if (lua_getmetatable(L, narg)) { /* 1 */
        lbC_Type *from_type;
        lbM_getfield(L, lbM_typeinfo); /* 2 */
        from_type = (lbC_Type*)lua_touserdata(L, -1);
        lua_pop(L, 2); /* (2)(1) */
        if (from_type != NULL && from_type->castfunc != NULL)
            return from_type->castfunc(L, narg, t);
    }
    return NULL;
}

int lbC_isa(lua_State *L, int narg, const lbC_Type *t) {
    return testudata(L, narg, t) || try_cast(L, narg, t) != NULL;
}

void *lbC_cast(lua_State *L, int ud, const lbC_Type *t) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (!check_size(L, ud) || obj == NULL || obj->instance == NULL)
        return NULL;
    return testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
}


/* lbind object maintain */

const char *lbO_tolstring(lua_State *L, int idx, size_t *plen) {
    const lbC_Type *t = NULL;
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL && check_size(L, 1)) {
        if (lua_getmetatable(L, 1)) { /* 1 */
            lbM_getfield(L, lbM_typeinfo); /* 2 */
            t = (const lbC_Type*)lua_touserdata(L, -1);
            lua_pop(L, 2); /* (2)(1) */
        }
        if (t != NULL) {
            if (obj->instance == NULL)
                lua_pushfstring(L, "%s: (null)", t->tname);
            else
                lua_pushfstring(L, "%s: %p", t->tname, obj->instance);
            return lua_tolstring(L, -1, plen);
        }
    }
    return luaL_tolstring(L, idx, plen);
}

void lbO_register(lua_State *L, const void *p, const lbC_Type *t) {
    get_pointertable(L); /* 1 */
    lua_rawgetp(L, -1, p); /* 2 */
    if (lua_isnil(L, -1)) {
        lbC_Object *obj;
        lua_pop(L, 1); /* (2) */
        obj = (lbC_Object*)lua_newuserdata(L, sizeof(lbC_Object)); /* 2 */
        obj->instance = (void*)p;
        obj->flags = t->init_flags;
        lbC_getmetatable(L, t); /* 3 */
        lua_setmetatable(L, -2); /* 3->2 */
        lua_pushvalue(L, -1); /* 2->3 */
        lua_rawsetp(L, -3, p); /* 3->1 */
    }
    lua_remove(L, -2); /* (1) */
}

void *lbO_unregister(lua_State *L, int ud) {
    void *u = NULL;
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (obj != NULL) {
        if (!check_size(L, ud))
            return NULL;
        if ((u = obj->instance) != NULL) {
            obj->instance = NULL;
            obj->flags &= ~LBC_GC;
#if LUA_VERSION_NUM < 502
            get_pointertable(L); /* 1 */
            lua_pushnil(L); /* 2 */
            lua_rawsetp(L, -3, u); /* 2->1 */
            lua_pop(L, 1); /* (1) */
#endif
        }
    }
    return u;
}

int lbO_retrieve(lua_State *L, const void *p) {
    get_pointertable(L); /* 1 */
    lua_rawgetp(L, -1, p); /* 2 */
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }
    lua_remove(L, -2);
    return 1;
}

void lbO_copyobject(lua_State *L, const void *obj, const lbC_Type *t) {
    lbC_getmetatable(L, t);
    lua_pushliteral(L, "new");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        lua_pushnil(L);
    }
    else {
        lua_remove(L, -2);
        lbO_register(L, obj, t);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        lbG_register(L, -1); /* enable autodeletion for copied stuff */
    }
}

void *lbO_checkobject(lua_State *L, int ud, const lbC_Type *t) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    void *u = NULL;
    if (!check_size(L, ud))
        luaL_argerror(L, ud, "invalid lbind userdata");
    if (obj != NULL && obj->instance == NULL)
        luaL_argerror(L, ud, "null lbind object");
    u = testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
    if (u == NULL)
        lbind_typeerror(L, ud, t->tname);
    return u;
}

void *lbO_testobject(lua_State *L, int ud, const lbC_Type *t) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    return testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
}

void *lbO_toobject(lua_State *L, int ud) {
    lbC_Object *obj = to_object(L, ud);
    return obj == NULL ? NULL : obj->instance;
}


/* parse string enumerate value */

/*#define skip_white(s) ((s) += strspn((s), " \t\r\n,|"))*/
#define skip_white(s) do { \
        while (*(s) == ' ' || *(s) == '\t' || *(s) == '\r' \
                || *(s) == '\n' || *(s) == ',' || *(s) == '|' \
              ) ++(s); } while (0)

static int parse_ident(lua_State *L, const char *p, int *value) {
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

static int parse_enum(lua_State *L, const char *s, int *penum, int check) {
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

int lbE_isenum(lua_State *L, int narg, lbE_EnumType *et) {
    return lua_isnumber(L, narg) || lua_isstring(L, narg);
}

void lbE_pushenum(lua_State *L, int evalue, lbE_EnumType *et) {
    lua_pushinteger(L, evalue);
}

static int toenum(lua_State *L, int narg, lbE_EnumType *et, int check) {
    const char *str;
    int value = lua_tointeger(L, narg);
    if (value != 0 || lua_isnumber(L, narg))
        return value;
    if ((str = lua_tostring(L, narg)) != NULL) {
        int success;
        lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
        if (et->flags & LBE_BITFIELD)
            success = parse_enum(L, str, &value, check);
        else {
            narg = lua_absindex(L, narg);
            lua_pushvalue(L, narg); /* narg->2 */
            lua_rawget(L, -2); /* 2->2 */
            success = (value = lua_tonumber(L, -1)) != 0 || lua_isnumber(L, -1);
        }
        lua_pop(L, 1); /* (2) */
        if (success)
            return value;
    }
    if (check)
        lbind_typeerror(L, narg, et->name);
    return 0;
}

int lbE_toenum(lua_State *L, int narg, lbE_EnumType *et) {
    return toenum(L, narg, et, 0);
}

int lbE_checkenum(lua_State *L, int narg, lbE_EnumType *et) {
    return toenum(L, narg, et, 1);
}

void lbE_initenum(lua_State *L, const char *name, lbE_Enum *enums, lbE_EnumType *et) {
    /* stack: etable */
    et->name = name;
    et->enums = enums;
    et->flags = 0;

    for (; enums->name != NULL; ++enums) {
        lua_pushstring(L, enums->name); /* 1 */
        lua_pushinteger(L, enums->value); /* 2 */
        lua_rawset(L, -3); /* 1,2->etable */
    }

    /* set metatable */
    get_enummeta(L); /* 1 */
    lua_setmetatable(L, -2); /* 1->etable */

    /* set typeinfo for enum table */
    lua_pushlightuserdata(L, et); /* 1 */
    lua_pushstring(L, "enum"); /* 2 */
    lua_rawset(L, -3); /* 1,2->etable */

    /* set global informations */
    lua_pushvalue(L, -1); /* etable->1 */
    register_global_info(L, et->name, et); /* (1) */

    /* set enum table to registry */
    lua_pushvalue(L, -1); /* 1 */
    lua_rawsetp(L, LUA_REGISTRYINDEX, et); /* 1->env */
}

void lbE_setbitflag(lua_State *L, int isbitflag, lbE_EnumType *et) {
    if (isbitflag)
        et->flags |= LBE_BITFIELD;
    else
        et->flags &= ~LBE_BITFIELD;
}


/* lbind Lua side runtime */

static int lbR_register(lua_State *L) {
    int i, top = lua_gettop(L);
    for (i = 1; i <= top; ++i)
        lbG_register(L, i);
    return top;
}

static int lbR_unregister(lua_State *L) {
    int i, top = lua_gettop(L);
    for (i = 1; i <= top; ++i)
        lbG_unregister(L, i);
    return top;
}

static int lbR_owner(lua_State *L) {
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

static int lbR_null(lua_State *L) {
    const void *u = lbO_toobject(L, -1);
    if (u != NULL)
        lua_pushlightuserdata(L, (void*)u);
    else
        lua_pushnil(L);
    return 1;
}

static int lbR_valid(lua_State *L) {
    const void *u = lua_touserdata(L, -1);
    if (u != NULL) {
        get_pointertable(L);
        lua_rawgetp(L, -1, u);
    }
    else
        lua_pushnil(L);
    return 1;
}

static int lbR_delete(lua_State *L) {
    lua_getfield(L, -1, "delete");
    if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, -2);
        lua_call(L, 1, 0);
    }
    return 0;
}

static int lbR_info(lua_State *L) {
#define INFOS \
    X( 1, "pointers",     get_pointertable(L)) \
    X( 2, "types",        get_typemap(L)) \
    X( 3, "libmap",       get_libmap(L)) \
    X( 4, "libmeta",      get_libmeta(L)) \
    X( 5, "enummeta",     get_enummeta(L)) \
    X( 6, "pointers_key", lua_pushlightuserdata(L, lbind_ptrbox)) \
    X( 7, "types_key",    lua_pushlightuserdata(L, lbind_typebox)) \
    X( 8, "libmap_key",   lua_pushlightuserdata(L, lbind_libbox)) \
    X( 9, "libmeta_key",  lua_pushlightuserdata(L, lbind_libmeta)) \
    X(10, "enummeta_key", lua_pushlightuserdata(L, lbind_enummeta)) \

    static const char *options[] = {
        "all",
#define X(a,b,c) b,
        INFOS
#undef  X
    };
#define TOTAL_OPTIONS (sizeof(options)/sizeof(options[0])-1)
    int option = 0;
    if (lua_isnoneornil(L, 1))
        lua_newtable(L);
    else if (lua_isstring(L, 1))
        option = luaL_checkoption(L, 1, options[0], options);
    luaL_checkstack(L, TOTAL_OPTIONS, "no space for lbind global info");
#define X(a,b,c) if (option == 0 || option == a) c;
    INFOS
#undef  X
    if (option == 0) {
        int i;
        for (i = TOTAL_OPTIONS; i >= 1; --i)
            lua_setfield(L, -i - 1, options[i]);
    }
    return 1;
#undef TOTAL_OPTIONS
#undef INFOS
}

static int is_type(lua_State *L, const void *t, int ch) {
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

static void get_typeptr(lua_State *L) {
    if (!lua_islightuserdata(L, -1)) {
        if (lua_isstring(L, -2))
            get_typemap(L); /* 1 */
        else
            get_libmap(L); /* 1 */
        lua_pushvalue(L, -2); /* 2 */
        lua_rawget(L, -2); /* 2->2 */
        lua_remove(L, -2); /* (1) */
    }
}

static int lbR_isa(lua_State *L) {
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

static int lbR_cast(lua_State *L) {
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

static int libtable2type(lua_State *L) {
    get_libmap(L); /* 1 */
    lua_pushvalue(L, -2); /* 2 */
    lua_rawget(L, -2); /* 2->2 */
    lua_remove(L, -2); /* (1) */
    if (lua_islightuserdata(L, -1))
        return 1;
    lua_pop(L, 1); /* (1) */
    return 0;
}

static int lbR_type(lua_State *L) {
    const char *type = NULL;
    if (lua_type(L, -1) == LUA_TUSERDATA)
        type = lbC_type(L, -1);
    else if (lua_islightuserdata(L, -1)
             || (lua_istable(L, -1) && libtable2type(L))) {
        lbC_Type *t = (lbC_Type*)lua_touserdata(L, -1);
        lua_rawgetp(L, LUA_REGISTRYINDEX, t);
        if (lua_istable(L, -1)) {
            const char *tag;
            lua_rawgetp(L, -1, t);
            tag = lua_tostring(L, -1);
            lua_pop(L, 2);
            if (tag != NULL && (*tag == 'c' || *tag == 'e'))
                type = t->tname;
        }
    }
    lua_pushstring(L, type != NULL ? type : lua_typename(L, -1));
    return 1;
}

static int get_classmt(lua_State *L) {
    return ((lua_islightuserdata(L, -1)
             && is_type(L, lua_touserdata(L, -1), 'c'))
            || (lua_istable(L, -1) && libtable2type(L))
            || (lua_getmetatable(L, -1)));
}

static int lbR_methods(lua_State *L) {
    if (!get_classmt(L)) return 0;
    lbM_getfield(L, lbM_libtable);
    return 1;
}

static int lbR_bases(lua_State *L) {
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

static int lbR_enum(lua_State *L) {
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

int luaopen_lbind(lua_State *L) {
    luaL_Reg liblbind[] = {
#define ENTRY(name) { #name, lbR_##name }
        ENTRY(bases),
        ENTRY(cast),
        ENTRY(delete),
        ENTRY(enum),
        ENTRY(info),
        ENTRY(isa),
        ENTRY(methods),
        ENTRY(null),
        ENTRY(owner),
        ENTRY(register),
        ENTRY(type),
        ENTRY(unregister),
        ENTRY(valid),
#undef ENTRY
        { NULL, NULL }
    };

    luaL_newlib(L, liblbind);
#if LUA_VERSION_NUM < 502
    lua_pushvalue(L, -1);
    lua_setglobal(L, "lbind");
#endif
    return 1;
}


/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-DLUA_BUILD_AS_DLL' output='lbind.dll'
 * cc: run='$lua tt.lua'
 */
