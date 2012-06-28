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

void luaL_setfuncs(lua_State *L, luaL_Reg *l, int nup) {
    luaL_checkstack(L, nup, "too many upvalues");
    for (; l->name != NULL; l++) {  /* fill the table with given functions */
        int i;
        for (i = 0; i < nup; i++)  /* copy upvalues to the top */
            lua_pushvalue(L, -nup);
        lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
        lua_setfield(L, -(nup + 2), l->name);
    }
    lua_pop(L, nup);  /* remove upvalues */
}

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
  return lua_tolstring(L, -1, plen);
}

#endif /* LUA_VERSION_NUM < 502 */


/* lbind class register */

void lbind_install(lua_State *L, lbind_Reg *reg) {
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
    const char *real_type = lbind_type(L, narg);
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
    lbind_Enum *et;
    get_libmeta(L); /* 1 */
    lua_pushvalue(L, 1); /* 2 */
    lua_rawget(L, -2); /* 2->2 */
    if ((et = (lbind_Enum*)lua_touserdata(L, -1)) == NULL)
        return 0;
    lua_pushinteger(L, lbind_checkenum(L, 2, et));
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
} lbind_Object;

#define LBC_GC          0x01
#define LBC_HASSETTER   0x02
#define LBC_HASGETTER   0x04

#define check_size(L,n) (lua_rawlen((L),(n)) == sizeof(lbind_Object))


/* lbind memory maintain */

static lbind_Object *to_object(lua_State *L, int ud) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, ud);
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

void lbind_track(lua_State *L, int narg) {
    lbind_Object *obj = to_object(L, narg);
    if (obj != NULL)
        obj->flags |= LBC_GC;
}

void lbind_untrack(lua_State *L, int narg) {
    lbind_Object *obj = to_object(L, narg);
    if (obj != NULL)
        obj->flags &= ~LBC_GC;
}

int lbind_hastrack(lua_State *L, int narg) {
    lbind_Object *obj = to_object(L, narg);
    return obj != NULL && (obj->flags & LBC_GC) != 0;
}


/* lbind class maintain */

#define lbM_basetable   "__bases"
#define lbM_libtable    "__methods"
#define lbM_typeinfo    "__typeinfo"
#define lbM_gettertable "__get"
#define lbM_settertable "__set"

#define lbM_getfield(L,f) lua_getfield(L,-1,lbM_##f)
#define lbM_setfield(L,f) lua_setfield(L,-2,lbM_##f)

static int base_setflags(lbind_Base *base, int flag, int set) {
    int old_flag = base->flags&flag;
    if (set)
        base->flags |= flag;
    else
        base->flags &= ~flag;
    return old_flag;
}

int lbind_setautotrack(lua_State *L, int autotrack, lbind_Type *t) {
    return base_setflags(&t->base, LBC_GC, autotrack);
}

void lbind_inittype(lua_State *L, const char *name, lbind_Type **bases, lbind_Type *t) {
    t->base.name = name;
    t->base.flags = 0;
    t->bases = bases;
    t->cast = NULL;
}

static int newlocal_helper(lua_State *L) {
    lua_getfield(L, lua_upvalueindex(1), "new");
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    lbind_track(L, 1);
    return lua_gettop(L);
}

static int Lnewindex(lua_State *L) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
    if (obj != NULL
            && (obj->flags & LBC_HASSETTER) != 0
            && lua_getmetatable(L, 1)) { /* 1 */
        lua_CFunction setter;
        /* setter table */
        lbM_getfield(L, settertable); /* 2 */
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

static int Lgc(lua_State *L) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
    if (obj != NULL && check_size(L, 1)) {
        if ((obj->flags & LBC_GC) != 0) {
            lua_getfield(L, 1, "delete");
            if (!lua_isnil(L, -1)) {
                lua_pushvalue(L, 1);
                lua_call(L, 1, 0);
            }
            if ((obj->flags & LBC_GC) != 0)
                lbind_unregister(L, 1);
        }
    }
    return 0;
}

static int Ltostring(lua_State *L) {
    lbind_tolstring(L, 1, NULL);
    return 1;
}

static int Lindex(lua_State *L) {
    int i;
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
    if (obj != NULL
            && (obj->flags & LBC_HASGETTER) != 0
            && lua_getmetatable(L, 1)) { /* 1 */
        lua_CFunction getter;
        /* getter table */
        lbM_getfield(L, gettertable); /* 2 */
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
            lbind_getmetatable(L, (const lbind_Type*)lua_touserdata(L, lua_upvalueindex(i)));
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            lbM_getfield(L, libtable);
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

static void push_indexfunc(lua_State *L, lbind_Type **bases) {
    int nups = 1; /* stack: libtable */
    if (bases != NULL) {
        for (; *bases != NULL; ++nups, ++bases) {
            lbind_getmetatable(L, *bases);
            if (lua_isnil(L, -1))
                lua_pushlightuserdata(L, *bases);
            else
                lbM_getfield(L, libtable);
            lua_remove(L, -2);
            luaL_checkstack(L, 1, "no space for base type");
        }
    }
    lua_pushcclosure(L, Lindex, nups);
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
     *   - registry:     lbind_Type*->metatable
     *   - libmap:       libtable->lbind_Type*
     *   - typeinfo:     name->lbind_Type*
     *   ( metatable:    __methods->libtable
     *                   __typeinfo->lbind_Type*
     *     lbind_Type*:    tname->name )
     * this is for name/libtable/metatable/lbind_Type* transform.
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

void lbind_setmt(lua_State *L, lbind_Type *t) {
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
    lua_pushcfunction(L, Lnewindex); /* 1 */
    set_default(L, -2, "__newindex"); /* 1->mt */
    lua_pushcfunction(L, Lgc); /* 1 */
    set_default(L, -2, "__gc"); /* 1->mt */
    lua_pushcfunction(L, Ltostring); /* 1 */
    set_default(L, -2, "__tostring"); /* 1->mt */

    /* set typeinfo for metatable */
    lua_pushliteral(L, "class"); /* 1 */
    lua_rawsetp(L, -3, t); /* 1->mt */
    lua_pushvalue(L, -2); /* libtable->1 */
    lbM_setfield(L, libtable); /* 1->mt */
    lua_pushlightuserdata(L, (void*)t); /* 1 */
    lbM_setfield(L, typeinfo); /* 1->mt */

    /* set global informations */
    lua_pushvalue(L, -2); /* libtable->1 */
    register_global_info(L, t->base.name, t); /* (1) */

    /* set metatable to registry */
    lua_rawsetp(L, LUA_REGISTRYINDEX, t); /* (mt) */
}

void lbind_setaccessor(lua_State *L, luaL_Reg *getters, luaL_Reg *setters, lbind_Type *t) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, t);
    if (getters) {
        lua_newtable(L);
        luaL_setfuncs(L, getters, 0);
        lbM_setfield(L, gettertable);
        t->base.flags |= LBC_HASGETTER;
    }
    if (setters) {
        lua_newtable(L);
        luaL_setfuncs(L, setters, 0);
        lbM_setfield(L, settertable);
        t->base.flags |= LBC_HASSETTER;
    }
    lua_pop(L, 1);
}

void lbind_setcast(lua_State *L, lbind_Cast *cast, lbind_Type *t) {
    t->cast = cast;
}

int lbind_getmetatable(lua_State *L, const lbind_Type *t) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, (const void*)t);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}


/* lbind type system */

static int testudata(lua_State *L, int ud, const lbind_Type *t) {
    if (lua_getmetatable(L, ud) /* does it have a metatable? */
            && lbind_getmetatable(L, t)) { /* get correct metatable */
        if (!lua_rawequal(L, -1, -2))  /* not the same? */
            return 0;  /* value is a userdata with wrong metatable */
        lua_pop(L, 2);  /* remove both metatables */
        return 1;
    }
    return 0;
}

const char *lbind_type(lua_State *L, int narg) {
    lbind_Object *obj = to_object(L, narg);
    if (obj != NULL && lua_getmetatable(L, narg)) { /* 1 */
        const lbind_Type *t = NULL;
        lbM_getfield(L, typeinfo); /* 2 */
        if (!lua_isnil(L, -1))
            t = (const lbind_Type*)lua_touserdata(L, -1);
        lua_pop(L, 2); /* (2)(1) */
        if (t != NULL)
            return t->base.name;
    }
    return NULL;
}

void *try_cast(lua_State *L, int narg, const lbind_Type *t) {
    if (lua_getmetatable(L, narg)) { /* 1 */
        lbind_Type *from_type;
        lbM_getfield(L, typeinfo); /* 2 */
        from_type = (lbind_Type*)lua_touserdata(L, -1);
        lua_pop(L, 2); /* (2)(1) */
        if (from_type != NULL && from_type->cast != NULL)
            return from_type->cast(L, narg, t);
    }
    return NULL;
}

int lbind_isa(lua_State *L, int narg, const lbind_Type *t) {
    return testudata(L, narg, t) || try_cast(L, narg, t) != NULL;
}

void *lbind_cast(lua_State *L, int ud, const lbind_Type *t) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, ud);
    if (!check_size(L, ud) || obj == NULL || obj->instance == NULL)
        return NULL;
    return testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
}


/* lbind object maintain */

const char *lbind_tolstring(lua_State *L, int idx, size_t *plen) {
    const lbind_Type *t = NULL;
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
    if (obj != NULL && check_size(L, 1)) {
        if (lua_getmetatable(L, 1)) { /* 1 */
            lbM_getfield(L, typeinfo); /* 2 */
            t = (const lbind_Type*)lua_touserdata(L, -1);
            lua_pop(L, 2); /* (2)(1) */
        }
        if (t != NULL) {
            if (obj->instance == NULL)
                lua_pushfstring(L, "%s: (null)", t->base.name);
            else
                lua_pushfstring(L, "%s: %p", t->base.name, obj->instance);
            return lua_tolstring(L, -1, plen);
        }
    }
    return luaL_tolstring(L, idx, plen);
}

void lbind_register(lua_State *L, const void *p, const lbind_Type *t) {
    get_pointertable(L); /* 1 */
    lua_rawgetp(L, -1, p); /* 2 */
    if (lua_isnil(L, -1)) {
        lbind_Object *obj;
        lua_pop(L, 1); /* (2) */
        obj = (lbind_Object*)lua_newuserdata(L, sizeof(lbind_Object)); /* 2 */
        obj->instance = (void*)p;
        obj->flags = t->base.flags;
        if (lbind_getmetatable(L, t)) /* 3 */
            lua_setmetatable(L, -2); /* 3->2 */
        lua_pushvalue(L, -1); /* 2->3 */
        lua_rawsetp(L, -3, p); /* 3->1 */
    }
    lua_remove(L, -2); /* (1) */
}

void *lbind_unregister(lua_State *L, int ud) {
    void *u = NULL;
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, ud);
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

int lbind_retrieve(lua_State *L, const void *p) {
    get_pointertable(L); /* 1 */
    lua_rawgetp(L, -1, p); /* 2 */
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }
    lua_remove(L, -2);
    return 1;
}

void lbind_copy(lua_State *L, const void *obj, const lbind_Type *t) {
    if (lbind_getmetatable(L, t)) {
        lua_pushliteral(L, "new");
        lua_rawget(L, -2);
    }
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        lua_pushnil(L);
    }
    else {
        lua_remove(L, -2);
        lbind_register(L, obj, t);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        lbind_track(L, -1); /* enable autodeletion for copied stuff */
    }
}

void *lbind_check(lua_State *L, int ud, const lbind_Type *t) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, ud);
    void *u = NULL;
    if (!check_size(L, ud))
        luaL_argerror(L, ud, "invalid lbind userdata");
    if (obj != NULL && obj->instance == NULL)
        luaL_argerror(L, ud, "null lbind object");
    u = testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
    if (u == NULL)
        lbind_typeerror(L, ud, t->base.name);
    return u;
}

void *lbind_test(lua_State *L, int ud, const lbind_Type *t) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, ud);
    return testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
}

void *lbind_isobject(lua_State *L, int ud) {
    lbind_Object *obj = to_object(L, ud);
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

#define LBE_COMBINE        1

int lbind_setcombine (lua_State *L, int combine, lbind_Enum *et) {
    return base_setflags(&et->base, LBE_COMBINE, combine);
}

int lbind_isenum(lua_State *L, int narg, lbind_Enum *et) {
    return lua_isnumber(L, narg) || lua_isstring(L, narg);
}

void lbind_pushenum(lua_State *L, int evalue, lbind_Enum *et) {
    lua_pushinteger(L, evalue);
}

static int toenum(lua_State *L, int narg, lbind_Enum *et, int check) {
    const char *str;
    int value = lua_tointeger(L, narg);
    if (value != 0 || lua_isnumber(L, narg))
        return value;
    if ((str = lua_tostring(L, narg)) != NULL) {
        int success;
        lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
        if (et->base.flags & LBE_COMBINE)
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
        lbind_typeerror(L, narg, et->base.name);
    return 0;
}

int lbind_toenum(lua_State *L, int narg, lbind_Enum *et) {
    return toenum(L, narg, et, 0);
}

int lbind_checkenum(lua_State *L, int narg, lbind_Enum *et) {
    return toenum(L, narg, et, 1);
}

void lbind_initenum(lua_State *L, const char *name, lbind_EnumItem *enums, lbind_Enum *et) {
    /* stack: etable */
    et->base.name = name;
    et->base.flags = 0;
    et->enums = enums;

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
    register_global_info(L, et->base.name, et); /* (1) */

    /* set enum table to registry */
    lua_pushvalue(L, -1); /* 1 */
    lua_rawsetp(L, LUA_REGISTRYINDEX, et); /* 1->env */
}


/* lbind Lua side runtime */

static int Lregister(lua_State *L) {
    int i, top = lua_gettop(L);
    for (i = 1; i <= top; ++i)
        lbind_track(L, i);
    return top;
}

static int Lunregister(lua_State *L) {
    int i, top = lua_gettop(L);
    for (i = 1; i <= top; ++i)
        lbind_untrack(L, i);
    return top;
}

static int Lowner(lua_State *L) {
    int i, top = lua_gettop(L);
    luaL_checkstack(L, top, "no space for owner info");
    for (i = 1; i <= top; ++i) {
        if (lbind_hastrack(L, i))
            lua_pushliteral(L, "Lua");
        else
            lua_pushliteral(L, "C");
    }
    return top;
}

static int Lnull(lua_State *L) {
    const void *u = lbind_isobject(L, -1);
    if (u != NULL)
        lua_pushlightuserdata(L, (void*)u);
    else
        lua_pushnil(L);
    return 1;
}

static int Lvalid(lua_State *L) {
    const void *u = lua_touserdata(L, -1);
    if (u != NULL) {
        get_pointertable(L);
        lua_rawgetp(L, -1, u);
    }
    else
        lua_pushnil(L);
    return 1;
}

static int Ldelete(lua_State *L) {
    lua_getfield(L, -1, "delete");
    if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, -2);
        lua_call(L, 1, 0);
    }
    return 0;
}

static int Linfo(lua_State *L) {
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

static int Lisa(lua_State *L) {
    get_typeptr(L);
    if (lua_islightuserdata(L, -1)) {
        const lbind_Type *t = (const lbind_Type*)lua_touserdata(L, -1);
        if (is_type(L, t, 'c')) {
            lua_pushboolean(L, lbind_isa(L, -3, t));
            return 1;
        }
    }
    return 0;
}

static int Lcast(lua_State *L) {
    get_typeptr(L);
    if (lua_islightuserdata(L, -1)) {
        const lbind_Type *t = (const lbind_Type*)lua_touserdata(L, -1);
        void *u;
        if (is_type(L, t, 'c') && (u = lbind_cast(L, -3, t)) != NULL) {
            lbind_register(L, u, t);
            return 1;
        }
    }
    return 0;
}

static int get_typeud(lua_State *L) {
    /* stack: (libtable|ud) */
    if (lua_islightuserdata(L, -1))
        return 1;
    else if (lua_isuserdata(L, -1)) {
        if (lua_getmetatable(L, -1)) { /* 1 */
            lua_remove(L, -2); /* (ud) */
            lbM_getfield(L, typeinfo); /* 2 */
            lua_remove(L, -2); /* (1) */
            if (lua_islightuserdata(L, -1))
                return 1;
        }
        lua_pop(L, 1); /* (ud) */
    }
    else if (lua_istable(L, -1)) {
        get_libmap(L); /* 1 */
        lua_insert(L, -2); /* 1->libtable */
        lua_rawget(L, -2); /* libtable->ud */
        if (lua_islightuserdata(L, -1))
            return 1;
        lua_pop(L, 1); /* (ud) */
    }
    return 0;
}

static int Ltype(lua_State *L) {
    const char *type = NULL;
    if (lua_type(L, -1) == LUA_TUSERDATA)
        type = lbind_type(L, -1);
    else if (get_typeud(L)) {
        lbind_Type *t = (lbind_Type*)lua_touserdata(L, -1);
        lua_rawgetp(L, LUA_REGISTRYINDEX, t);
        if (lua_istable(L, -1)) {
            const char *tag;
            lua_rawgetp(L, -1, t);
            tag = lua_tostring(L, -1);
            lua_pop(L, 2);
            if (tag != NULL && (*tag == 'c' || *tag == 'e'))
                type = t->base.name;
        }
    }
    lua_pushstring(L, type != NULL ? type : lua_typename(L, -1));
    return 1;
}

static int get_classmt(lua_State *L) {
    if (get_typeud(L)) {
        lbind_Type *t = (lbind_Type*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        return lbind_getmetatable(L, t);
    }
    return 0;
}

static int Lmethods(lua_State *L) {
    if (!get_classmt(L)) return 0;
    lbM_getfield(L, libtable);
    return 1;
}

static int Lbases(lua_State *L) {
    lbind_Type *t;
    int incomplete = 0;
    if (!get_classmt(L)) return 0;
    lbM_getfield(L, basetable); /* 2 */
    if (!lua_isnil(L, -1)) {
        lua_pushliteral(L, "incomplete"); /* 3 */
        lua_rawget(L, -2); /* 3->3 */
        incomplete = lua_toboolean(L, -1);
        lua_pop(L, 1); /* (3) */
        if (!incomplete)
            return 1;
    }
    lbM_getfield(L, typeinfo); /* 3 */
    if ((t = (lbind_Type*)lua_touserdata(L, -1)) != NULL) {
        int i;
        lbind_Type **bi;
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
                lbind_getmetatable(L, *bi); /* 3 */
                if (lua_isnil(L, -1)) {
                    lua_pushlightuserdata(L, *bi); /* 4 */
                    incomplete = 1;
                }
                else
                    lbM_getfield(L, libtable); /* 4 */
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
        lbM_setfield(L, basetable); /* 4->3 */
        lua_pop(L, 1); /* (3) */
        return 1;
    }
    return 0;
}

static int Lenum(lua_State *L) {
    get_typeptr(L);
    if (lua_islightuserdata(L, -1)) {
        int value, first = 1;
        luaL_Buffer b;
        lbind_EnumItem *ei;
        lbind_Enum *et = (lbind_Enum*)lua_touserdata(L, -1);
        if (!is_type(L, et, 'e'))
            return 0;
        lua_pop(L, 1); /* pop enum table from is_type */
        value = lbind_checkenum(L, -2, et);
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
#define ENTRY(name) { #name, L##name }
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
