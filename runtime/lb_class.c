#define LUA_LIB
#include "lbind.h"


#if LUA_VERSION_NUM < 502
void lua_rawgetp(lua_State *L, int narg, const void *p)
{
    lua_pushlightuserdata(L, (void*)p);
    lua_rawget(L, narg < 0 ? narg - 1 : narg);
}

void lua_rawsetp(lua_State *L, int narg, const void *p)
{
    lua_pushlightuserdata(L, (void*)p);
    lua_insert(L, -2);
    lua_rawset(L, narg < 0 ? narg - 1 : narg);
}
#endif


/* lbind global informations */

#define lbind_ptrbox  "lbind-pointer-box"
#define lbind_typebox "lbind-typeinfo-box"
#define lbind_libmeta "lbind-lib-metatable"

static void rawget_staticptr(lua_State *L, const char *staticptr, const void **pcache)
{
    if (*pcache == NULL) {
        lua_pushstring(L, staticptr); /* 1 */
        lua_rawget(L, LUA_REGISTRYINDEX); /* 1->1 */
        if (lua_islightuserdata(L, -1))
            *pcache = lua_touserdata(L, -1);
        else {
            lua_pushstring(L, staticptr); /* 2 */
            lua_pushlightuserdata(L, (void*)staticptr); /* 3 */
            lua_rawset(L, LUA_REGISTRYINDEX); /* 2,3->env */
            *pcache = (const void*)staticptr;
        }
        lua_pop(L, 1); /* (1) */
    }
    lua_rawgetp(L, LUA_REGISTRYINDEX, *pcache); /* 1 */
}

void lbind_getpointertable(lua_State *L)
{
    static const char ptrbox_archor[] = lbind_ptrbox;
    static const void *ptrbox = NULL;

    rawget_staticptr(L, ptrbox_archor, &ptrbox); /* 1 */
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); /* (1) */
        lua_newtable(L); /* 1 */
        lua_newtable(L); /* 2 */
        lua_pushliteral(L, "__mode"); /* 3 */
        lua_pushliteral(L, "v"); /* 4 */
        lua_rawset(L, -3); /* 3,4->2 */
        lua_setmetatable(L, -2); /* 2->1 */
        lua_pushvalue(L, -1); /* 1->2 */
        lua_rawsetp(L, LUA_REGISTRYINDEX, ptrbox); /* 2->env */
    }
}

void lbind_gettypeinfotable(lua_State *L)
{
    static const char typeinfo_archor[] = lbind_typebox;
    static const void *typeinfo = NULL;

    rawget_staticptr(L, typeinfo_archor, &typeinfo); /* 1 */
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); /* (1) */
        lua_newtable(L); /* 1 */
        lua_pushvalue(L, -1); /* 1->2 */
        lua_rawsetp(L, LUA_REGISTRYINDEX, typeinfo); /* 2->env */
    }
}

static int libcall_helper(lua_State *L)
{
    lua_getfield(L, 1, "new_local");
    if (lua_isnil(L, -1))
        return luaL_argerror(
                   L, 1, "no new_local function found");
    lua_replace(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

static void get_libmetatable(lua_State *L)
{
    static const char libmeta_archor[] = lbind_libmeta;
    static const void *libmeta = NULL;

    rawget_staticptr(L, libmeta_archor, &libmeta); /* 1 */
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); /* (1) */
        lua_createtable(L, 0, 1); /* 1 */
        lua_pushliteral(L, "__call"); /* 2 */
        lua_pushcfunction(L, libcall_helper); /* 3 */
        lua_rawset(L, -3); /* 2,3->1 */
        lua_pushvalue(L, -1); /* 1->2 */
        lua_rawsetp(L, LUA_REGISTRYINDEX, libmeta); /* 2->env */
    }
}

int lbind_getinfo(lua_State *L)
{
#define INFOS \
    X(1, "pointers",     lbind_getpointertable(L)) \
    X(2, "pointers_key", lua_getfield(L, LUA_REGISTRYINDEX, lbind_ptrbox)) \
    X(3, "types",        lbind_gettypeinfotable(L)) \
    X(4, "types_key",    lua_getfield(L, LUA_REGISTRYINDEX, lbind_typebox)) \
    X(5, "libmeta",      get_libmetatable(L)) \
    X(6, "libmeta_key",  lua_getfield(L, LUA_REGISTRYINDEX, lbind_libmeta)) \
     
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


/* lbind userdata maintain */

typedef struct {
    void *instance;
    int flags;
} lbC_Object;

#define LBC_GC          0x01
#define LBC_HASSETTER   0x02
#define LBC_HASGETTER   0x04

void lbind_getmetatable(lua_State *L, const lbC_Type *t)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, t);
}

void lbind_pushudata(lua_State *L, const void *p, const lbC_Type *t)
{
    lbind_getpointertable(L);
    lua_rawgetp(L, -1, p);
    lua_remove(L, -2);
    if (lua_isnil(L, -1)) {
        lbC_Object *obj;
        lua_pop(L, 1);
        obj = (lbC_Object*)lua_newuserdata(L, sizeof(lbC_Object)); /* 1 */
        obj->instance = (void*)p;
        obj->flags = t->init_flags;
        lbind_getmetatable(L, t); /* 2 */
        lua_setmetatable(L, -2); /* 2->1 */
        lbind_getpointertable(L); /* 2 */
        lua_pushvalue(L, -2); /* 1->3 */
        lua_rawsetp(L, -2, p); /* 3->2 */
        lua_pop(L, 1); /* (2) */
    }
}

void lbind_copyudata(lua_State *L, const void *obj, const lbC_Type *t)
{
    lbind_getmetatable(L, t);
    lua_pushstring(L, "new");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        lua_pushnil(L);
    }
    else {
        lua_remove(L, -2);
        lbind_pushudata(L, obj, t);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        lbG_register(L, -1); /* enable autodeletion for copied stuff */
    }
}

static lbC_Object *to_object(lua_State *L, int ud)
{
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (obj != NULL) {/* && obj->instance != NULL) { */
        lbind_getpointertable(L); /* 1 */
        /* XXX: if obj is not a lbind userdata below line may crash.
         * has any better ideas?  */
        lua_rawgetp(L, -1, obj->instance); /* 2 */
        if (lua_isnil(L, -1))
            obj = NULL;
        lua_pop(L, 2); /* (2)(1) */
    }
    return obj;
}

void *lbind_toudata(lua_State *L, int ud)
{
    lbC_Object *obj = to_object(L, ud);
    return obj == NULL ? NULL : obj->instance;
}

static int testudata(lua_State *L, int ud, const lbC_Type *t)
{
    if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
        lbind_getmetatable(L, t); /* get correct metatable */
        if (!lua_rawequal(L, -1, -2))  /* not the same? */
            return 0;  /* value is a userdata with wrong metatable */
        lua_pop(L, 2);  /* remove both metatables */
        return 1;
    }
    return 0;
}

void *lbind_testudata(lua_State *L, int ud, const lbC_Type *t)
{
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (obj != NULL && testudata(L, ud, t))
        return obj->instance;
    return NULL;
}

void *lbind_checkudata(lua_State *L, int ud, const lbC_Type *t)
{
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (obj != NULL && obj->instance == NULL)
        luaL_argerror(L, ud, "null lbind object");
    else if (obj == NULL || !testudata(L, ud, t))
        lbE_typeerror(L, ud, t->tname);
    return obj->instance;
}

void *lbind_eraseudata(lua_State *L, int ud)
{
    void *u = NULL;
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    if (obj != NULL) {
        lbind_getpointertable(L); /* 1 */
        lua_rawgetp(L, -1, obj->instance); /* 2 */
        if ((obj = (lbC_Object*)lua_touserdata(L, -1)) != NULL
                && (u = obj->instance) != NULL) {
            obj->instance = NULL;
            obj->flags &= ~LBC_GC;
            lua_pushnil(L); /* 3 */
            lua_rawsetp(L, -3, u); /* 3->1 */
        }
        lua_pop(L, 2); /* (2)(1) */
    }
    return u;
}


/* lbind memory maintain */

void lbG_register(lua_State *L, int narg)
{
    lbC_Object *obj = to_object(L, narg);
    if (obj != NULL)
        obj->flags |= LBC_GC;
}

void lbG_unregister(lua_State *L, int narg)
{
    lbC_Object *obj = to_object(L, narg);
    if (obj != NULL)
        obj->flags &= ~LBC_GC;
}

int lbG_shouldgc(lua_State *L, int narg)
{
    lbC_Object *obj = to_object(L, narg);
    return obj != NULL && (obj->flags & LBC_GC) != 0;
}


/* lbind type system */

const char *lbC_type(lua_State *L, int narg)
{
    lbC_Object *obj = to_object(L, narg);
    if (obj != NULL) {
        const lbC_Type *ti;
        lua_getmetatable(L, narg);
        lbM_getfield(L, lbM_typeinfo);
        ti = (const lbC_Type*)lua_touserdata(L, -1);
        lua_pop(L, 2);
        if (ti != NULL)
            return ti->tname;
    }
    return NULL;
}

int lbC_isa(lua_State *L, int narg, const lbC_Type *t)
{
    if (testudata(L, narg, t))
        return 1;
    else {
        lbC_Type *from_type;
        lua_getmetatable(L, narg);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return 0;
        }
        lbM_getfield(L, lbM_typeinfo);
        from_type = (lbC_Type*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        if (from_type != NULL && from_type->testfunc != NULL) {
            return from_type->testfunc(L, narg, t);
        }
    }
    return 0;
}

void *lbC_cast(lua_State *L, int narg, const lbC_Type *t)
{
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, narg);
    lbC_Type *from_type;
    if (obj == NULL || obj->instance == NULL)
        return NULL;
    if (testudata(L, narg, t))
        return obj->instance;
    lua_getmetatable(L, narg);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }
    lbM_getfield(L, lbM_typeinfo);
    from_type = (lbC_Type*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (from_type == NULL || from_type->castfunc == NULL)
        return NULL;
    return from_type->castfunc(L, narg, t);
}

void *lbC_checkself(lua_State *L, int ud, const lbC_Type *t)
{
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    void *u = NULL;
    if (obj != NULL && obj->instance == NULL)
        luaL_argerror(L, ud, "null lbind object");
    else if ((u = lbC_cast(L, ud, t)) == NULL)
        lbE_typeerror(L, ud, t->tname);
    return u;
}


/* lbind class maintain */

luaL_Reg lbC_nomt[1] = {{NULL, NULL}};

void lbC_inittype(lua_State *L, const char *tname, lbC_Type **bases, lbC_Type *t)
{
    t->tname = tname;
    t->bases = bases;
    t->init_flags = 0;
    t->testfunc = NULL;
    t->castfunc = NULL;
}

static int newlocal_helper(lua_State *L)
{
    lua_getfield(L, lua_upvalueindex(1), "new");
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    lbG_register(L, 1);
    return lua_gettop(L);
}

static void lbC_setmethods(lua_State *L, luaL_Reg *funcs)
{
    luaL_setfuncs(L, funcs, 0);

    /* add new_local function */
    lua_pushliteral(L, "new"); /* 1 */
    lua_rawget(L, -2); /* 1->1 */
    lua_pushliteral(L, "new_local"); /* 2 */
    lua_pushvalue(L, -1); /* 2->3 */
    lua_rawget(L, -4); /* 3->3 */
    if (!lua_isnil(L, -3) && lua_isnil(L, -1)) {
        lua_pushvalue(L, -2); /* 4 */
        lua_pushvalue(L, -5); /* 5 */
        lua_pushcclosure(L, newlocal_helper, 1); /* 5->5 */
        lua_rawset(L, -6); /* 4,5->libtable */
    }
    lua_pop(L, 3);

    /* set metatable to libtable */
    get_libmetatable(L); /* 3 */
    lua_setmetatable(L, -2); /* 3->1 */
}

static int lbM_newindex(lua_State *L)
{
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL && (obj->flags & LBC_HASSETTER) != 0) {
        /* setter table */
        lua_getmetatable(L, 1); /* 1 */
        if (!lua_isnil(L, -1)) {
            lbM_getfield(L, lbM_settertable); /* 2 */
            lua_pushvalue(L, 2); /* 3 */
            lua_rawget(L, -2); /* 3->3 */
            if (lua_iscfunction(L, -1)) {
                lua_CFunction setter = lua_tocfunction(L, -1);
                return setter(L);
            }
        }
    }
    /* set it in uservalue table */
    if (lua_isuserdata(L, 1)) {
        lua_settop(L, 3);
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

static int lbM_gc(lua_State *L)
{
    lbC_Object *obj = to_object(L, 1);
    if (obj != NULL) {
        lua_getfield(L, 1, "delete");
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, 1);
            lua_call(L, 1, 0);
        }
        if (obj->instance != NULL)
            lbind_eraseudata(L, 1);
    }
    return 0;
}

static int lbM_tostring(lua_State *L)
{
    const lbC_Type *t = NULL;
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL) {
        lua_getmetatable(L, 1);
        if (!lua_isnil(L, -1)) {
            lbM_getfield(L, lbM_typeinfo);
            t = (const lbC_Type*)lua_touserdata(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (t != NULL) {
            if (obj->instance == NULL)
                lua_pushfstring(L, "%s: (null)", t->tname);
            else
                lua_pushfstring(L, "%s: %p", t->tname, obj->instance);
            return 1;
        }
    }
    lua_pushliteral(L, "(invalid lbind object)");
    return 1;
}

static int lbM_index(lua_State *L)
{
    int i;
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, 1);
    if (obj != NULL && (obj->flags & LBC_HASGETTER) != 0) {
        /* getter table */
        lua_getmetatable(L, 1); /* 1 */
        if (!lua_isnil(L, -1)) {
            lbM_getfield(L, lbM_gettertable); /* 2 */
            lua_pushvalue(L, 2); /* 3 */
            lua_rawget(L, -2); /* 3->3 */
            if (lua_iscfunction(L, -1)) {
                lua_CFunction getter = lua_tocfunction(L, -1);
                return getter(L);
            }
        }
    }
    /* find in libtable/superlibtable */
    lua_settop(L, 2);
    for (i = 1; !lua_isnone(L, lua_upvalueindex(i)); ++i) {
        if (lua_islightuserdata(L, lua_upvalueindex(i))) {
            lbind_getmetatable(L, (const lbC_Type*)lua_touserdata(L, lua_upvalueindex(i)));
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

static void push_indexfunc(lua_State *L, lbC_Type **bases)
{
    int nups = 1; /* stack: libtable */
    if (bases != NULL) {
        for (; *bases != NULL; ++nups, ++bases) {
            lbind_getmetatable(L, *bases);
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

void lbC_setmt(lua_State *L, luaL_Reg *funcs, luaL_Reg *mts, lbC_Type *t)
{
    /* stack: libtable metatable */
    lua_pushvalue(L, -2);
    lbC_setmethods(L, funcs);
    lua_pop(L, 1);

    /* init type metatable */
    lua_pushvalue(L, -2); /* 1->3 */
    push_indexfunc(L, t->bases); /* 3->3 */
    lua_setfield(L, -2, "__index"); /* 3->2 */
    lua_pushcfunction(L, lbM_newindex); /* 3 */
    lua_setfield(L, -2, "__newindex"); /* 3->2 */
    lua_pushcfunction(L, lbM_tostring); /* 3->3 */
    lua_setfield(L, -2, "__tostring"); /* 3->2 */
    if (mts != NULL && mts != lbC_nomt)
        luaL_setfuncs(L, mts, 0);
    lua_pushcfunction(L, lbM_gc); /* 3->3 */
    lua_setfield(L, -2, "__gc"); /* 3->2 */

    /* set global informations */
    lua_pushvalue(L, -2); /* 1->3 */
    lbM_setfield(L, lbM_libtable); /* 3->2 */
    lua_pushlightuserdata(L, (void*)t); /* 3 */
    lbM_setfield(L, lbM_typeinfo); /* 3->2 */
    lbind_gettypeinfotable(L); /* 3 */
    lua_pushstring(L, t->tname); /* 4 */
    lua_pushlightuserdata(L, (void*)t); /* 5 */
    lua_rawset(L, -3); /* 4,5->3 */
    lua_pop(L, 1); /* (3) */
    lua_rawsetp(L, LUA_REGISTRYINDEX, t); /* (2) */
}

void lbC_setaccessor(lua_State *L, luaL_Reg *getters, luaL_Reg *setters, lbC_Type *t)
{
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

void lbC_setcast(lua_State *L, lbC_testfunc tf, lbC_castfunc cf, lbC_Type *t)
{
    t->testfunc = tf;
    t->castfunc = cf;
}

/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-DLUA_BUILD_AS_DLL' input='lbind.c lb_class.c' output='lbind.dll'
 * cc: run='$lua tt.lua'
 */
