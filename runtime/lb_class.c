#define LUA_LIB
#include "lbind.h"


#if LUA_VERSION_NUM < 502
void lua_rawgetp(lua_State *L, int narg, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_rawget(L, narg < 0 ? narg - 1 : narg);
}

void lua_rawsetp(lua_State *L, int narg, const void *p) {
    lua_pushlightuserdata(L, (void*)p);
    lua_insert(L, -2);
    lua_rawset(L, narg < 0 ? narg - 1 : narg);
}

int lua_absindex(lua_State *L, int idx) {
    return (idx > 0 || idx <= LUA_REGISTRYINDEX)
           ? idx
           : lua_gettop(L) + idx + 1;
}
#endif


/* lbind global informations */

#define lbind_ptrbox        "lbind-pointer-box"
#define lbind_typebox       "lbind-typeinfo-box"
#define lbind_libbox        "lbind-libtable-box"
#define lbind_enummeta      "lbind-enum-metatable"
#define lbind_libmeta       "lbind-lib-metatable"

static int rawget_staticptr(lua_State *L, const char *staticptr, const void **pcache) {
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
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

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

void lbind_getpointertable(lua_State *L) {
    static const char ptrbox_archor[] = lbind_ptrbox;
    static const void *ptrbox = NULL;

    /*printf("lbind_getpointertable!!\n");*/
    if (!rawget_staticptr(L, ptrbox_archor, &ptrbox))
        setnewtable(L, ptrbox, "v");
    /*printf("ptrbox: %s\n", luaL_tolstring(L, -1, NULL));*/
    /*lua_pop(L, 1);*/
}

void lbind_gettypemaptable(lua_State *L) {
    static const char typeinfo_archor[] = lbind_typebox;
    static const void *typeinfo = NULL;

    if (!rawget_staticptr(L, typeinfo_archor, &typeinfo))
        setnewtable(L, typeinfo, NULL);
}

void lbind_getlibmaptable(lua_State *L) {
    static const char libbox_archor[] = lbind_libbox;
    static const void *libbox = NULL;

    if (!rawget_staticptr(L, libbox_archor, &libbox))
        setnewtable(L, libbox, "k");
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

void lbind_getlibmeta(lua_State *L) {
    static const char libmeta_archor[] = lbind_libmeta;
    static const void *libmeta = NULL;

    if (!rawget_staticptr(L, libmeta_archor, &libmeta)) {
        setnewtable(L, libmeta, NULL);
        lua_pushliteral(L, "__call"); /* 2 */
        lua_pushcfunction(L, libcall_helper); /* 3 */
        lua_rawset(L, -3); /* 2,3->1 */
    }
}

static int enumcall_helper(lua_State *L) {
    lbE_EnumType *et;
    lbind_getlibmeta(L); /* 1 */
    lua_pushvalue(L, 1); /* 2 */
    lua_rawget(L, -2); /* 2->2 */
    if ((et = (lbE_EnumType*)lua_touserdata(L, -1)) == NULL)
        return 0;
    lua_pushinteger(L, lbE_checkenum(L, 2, et));
    return 1;
}

void lbind_getenummeta(lua_State *L) {
    static const char enummeta_archor[] = lbind_enummeta;
    static const void *enummeta = NULL;

    if (!rawget_staticptr(L, enummeta_archor, &enummeta)) {
        setnewtable(L, enummeta, NULL);
        lua_pushliteral(L, "__call"); /* 2 */
        lua_pushcfunction(L, enumcall_helper); /* 3 */
        lua_rawset(L, -3); /* 2,3->1 */
    }
}

int lbind_getinfo(lua_State *L) {
#define INFOS \
    X( 1, "pointers",     lbind_getpointertable(L)) \
    X( 2, "types",        lbind_gettypemaptable(L)) \
    X( 3, "libmap",       lbind_getlibmaptable(L)) \
    X( 4, "libmeta",      lbind_getlibmeta(L)) \
    X( 5, "enummeta",     lbind_getenummeta(L)) \
    X( 6, "pointers_key", lua_getfield(L, LUA_REGISTRYINDEX, lbind_ptrbox)) \
    X( 7, "types_key",    lua_getfield(L, LUA_REGISTRYINDEX, lbind_typebox)) \
    X( 8, "libmap_key",   lua_getfield(L, LUA_REGISTRYINDEX, lbind_libbox)) \
    X( 9, "libmeta_key",  lua_getfield(L, LUA_REGISTRYINDEX, lbind_libmeta)) \
    X(10, "enummeta_key", lua_getfield(L, LUA_REGISTRYINDEX, lbind_enummeta)) \
     
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

#define check_size(L,n) (lua_rawlen((L),(n)) == sizeof(lbC_Object))

/* lbind memory maintain */

static lbC_Object *to_object(lua_State *L, int ud) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    printf("to_object: %d\n", ud);
    if (obj != NULL) {
        if (!check_size(L, ud) || obj->instance == NULL)
            obj = NULL;
        else {
            ud = lua_absindex(L, ud);
            lbind_getpointertable(L); /* 1 */
            printf("instance: %p\n", obj->instance);
            lua_rawgetp(L, -1, obj->instance); /* 2 */
            printf("ptrbox: %s\n", luaL_typename(L, -1));
            if (!lua_rawequal(L, ud, -1))
                obj = NULL;
            lua_pop(L, 2); /* (2)(1) */
            printf("object: %p\n", (void*)obj);
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

luaL_Reg lbC_nofunc[1] = {{NULL, NULL}};

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
        printf("gcced: %p\n", obj->instance);
        if ((obj->flags & LBC_GC) != 0) {
            printf("not gcced yet\n");
            lua_getfield(L, 1, "delete");
            printf("delete: %s\n", luaL_typename(L, -1));
            if (!lua_isnil(L, -1)) {
                printf("call delete()\n");
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
            return 1;
        }
    }
    lua_pushliteral(L, "(invalid lbind object)");
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
    lbind_getlibmeta(L); /* 1 */
    lua_setmetatable(L, -3); /* 1->libtable */

    /* init type metatable */
    lua_pushvalue(L, -2); /* libtable->1 */
    push_indexfunc(L, t->bases); /* 1->1 */
    lua_setfield(L, -2, "__index"); /* 1->mt */
    lua_pushcfunction(L, lbM_newindex); /* 1 */
    lua_setfield(L, -2, "__newindex"); /* 1->mt */
    lua_pushcfunction(L, lbM_tostring); /* 1 */
    lua_setfield(L, -2, "__tostring"); /* 1->mt */
    lua_pushcfunction(L, lbM_gc); /* 1 */
    lua_setfield(L, -2, "__gc"); /* 1->mt */

    /* set typeinfo for metatable */
    lua_pushliteral(L, "class"); /* 1 */
    lua_rawsetp(L, -3, t); /* 1->mt */
    lua_pushvalue(L, -2); /* libtable->1 */
    lbM_setfield(L, lbM_libtable); /* 1->mt */
    lua_pushlightuserdata(L, (void*)t); /* 1 */
    lbM_setfield(L, lbM_typeinfo); /* 1->mt */

    /* set global informations */
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
    lbind_gettypemaptable(L); /* 1 */
    lua_pushstring(L, t->tname); /* 2 */
    lua_pushlightuserdata(L, (void*)t); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 1); /* (1) */
    lbind_getlibmaptable(L); /* 1 */
    lua_pushvalue(L, -3); /* libtable->2 */
    lua_pushlightuserdata(L, (void*)t); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 1); /* 1 */

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

void lbO_register(lua_State *L, const void *p, const lbC_Type *t) {
    printf("lbO_register: %p\n", p);
    lbind_getpointertable(L); /* 1 */
    lua_rawgetp(L, -1, p); /* 2 */
    if (lua_isnil(L, -1)) {
        lbC_Object *obj;
        lua_pop(L, 1); /* (2) */
        obj = (lbC_Object*)lua_newuserdata(L, sizeof(lbC_Object)); /* 2 */
        obj->instance = (void*)p;
        obj->flags = t->init_flags;
        printf("create: %p\n", (void*)obj);
        lbC_getmetatable(L, t); /* 3 */
        lua_setmetatable(L, -2); /* 3->2 */
        lua_pushvalue(L, -1); /* 2->3 */
        lua_rawsetp(L, -3, p); /* 3->1 */
        lbind_getpointertable(L);
        lua_rawgetp(L, -1, p);
        if (!lua_isnil(L, -1)) {
            lbC_Object *obj = lua_touserdata(L, -1);
            printf("test ok: %p\n", obj->instance);
        }
        lua_pop(L, 2);
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
            lbind_getpointertable(L); /* 1 */
            lua_pushnil(L); /* 2 */
            lua_rawsetp(L, -3, u); /* 2->1 */
            lua_pop(L, 1); /* (1) */
#endif
        }
    }
    return u;
}

int lbO_retrieve(lua_State *L, const void *p) {
    lbind_getpointertable(L); /* 1 */
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

void *lbO_isobject(lua_State *L, int ud, const lbC_Type *t) {
    lbC_Object *obj = (lbC_Object*)lua_touserdata(L, ud);
    return testudata(L, ud, t) ? obj->instance : try_cast(L, ud, t);
}

void *lbO_toobject(lua_State *L, int ud) {
    lbC_Object *obj = to_object(L, ud);
    return obj == NULL ? NULL : obj->instance;
}


/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-DLUA_BUILD_AS_DLL' input='lb*.c' output='lbind.dll'
 * cc: run='$lua tt.lua'
 */
