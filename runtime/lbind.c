/* vim: set sw=2: */
#define LUA_LIB
#include "lbind.h"


#include <assert.h>
#include <ctype.h>


static int relate_index(int idx, int onstack) {
  return (idx > 0 || idx <= LUA_REGISTRYINDEX)
    ? idx
    : idx - onstack;
}

/* compatible apis */
#if LUA_VERSION_NUM < 502
#  define LUA_OK                        0
#  define lua_getuservalue              lua_getfenv
#  define lua_setuservalue              lua_setfenv
#  define lua_rawlen                    lua_objlen

static void lua_rawgetp(lua_State *L, int idx, const void *p) {
  lua_pushlightuserdata(L, (void*)p);
  lua_rawget(L, relate_index(idx, 1));
}

static void lua_rawsetp(lua_State *L, int idx, const void *p) {
  lua_pushlightuserdata(L, (void*)p);
  lua_insert(L, -2);
  lua_rawset(L, relate_index(idx, 1));
}

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

const char *luaL_tolstring(lua_State *L, int idx, size_t *plen) {
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

void luaL_traceback(lua_State *L, lua_State *L1, const char *msg, int level) {
  lua_Debug ar;
  int top = lua_gettop(L);
  int numlevels = countlevels(L1);
  int mark = (numlevels > LEVELS1 + LEVELS2) ? LEVELS1 : 0;
  if (msg) lua_pushfstring(L, "%s\n", msg);
  lua_pushliteral(L, "stack traceback:");
  while (lua_getstack(L1, level++, &ar)) {
    if (level == mark) {  /* too many levels? */
      lua_pushliteral(L, "\n\t...");  /* add a '...' */
      level = numlevels - LEVELS2;  /* and skip to last ones */
    }
    else {
      lua_getinfo(L1, "Slnt", &ar);
      lua_pushfstring(L, "\n\t%s:", ar.short_src);
      if (ar.currentline > 0)
        lua_pushfstring(L, "%d:", ar.currentline);
      lua_pushliteral(L, " in ");
      pushfuncname(L, &ar);
      if (ar.istailcall)
        lua_pushliteral(L, "\n\t(...tail calls...)");
      lua_concat(L, lua_gettop(L) - top);
    }
  }
  lua_concat(L, lua_gettop(L) - top);
}

#endif /* LUA_VERSION_NUM < 502 */


/* lbind class register */

void lbind_install(lua_State *L, lbind_Reg *libs) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD"); /* 1 */
  if (libs != NULL) {
    for (; libs->name != NULL; ++libs) {
      lua_pushstring(L, libs->name); /* 2 */
      lua_pushcfunction(L, libs->open_func); /* 3 */
      lua_rawset(L, -3); /* 2,3->1 */
    }
  }
  lua_pushstring(L, "lbind"); /* 2 */
  lua_pushcfunction(L, luaopen_lbind); /* 3 */
  lua_rawset(L, -3); /* 2,3->1 */
  lua_pop(L, 1); /* (1) */
}

int lbind_requiref(lua_State *L, const char *name, lua_CFunction loader) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED"); /* 1 */
  lua_getfield(L, -1, name); /* 2 */
  if (!lua_isnil(L, -1)) {
    lua_remove(L, -2); /* (1) */
    return 0;
  }
  lua_pop(L, 1);
  lua_pushstring(L, name); /* 2 */
  lua_pushcfunction(L, loader); /* 3 */
  lua_pushvalue(L, -2); /* 2->4 */
  lua_call(L, 1, 1); /* 3,4->3 */
  lua_pushvalue(L, -1); /* 3->4 */
  lua_insert(L, -4); /* 4->1 */
  /* stack: lib _LOADED name lib */
  lua_rawset(L, -3); /* 3,4->2 */
  lua_pop(L, 1); /* (2) */
  return 1;
}

void lbind_requirelibs(lua_State *L, lbind_Reg *libs) {
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED"); /* 1 */
  for (; libs->name != NULL; ++libs) {
    lua_pushstring(L, libs->name); /* 2 */
    lua_pushvalue(L, -1); /* 3 */
    lua_rawget(L, -3); /* 3->3 */
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      continue;
    }
    lua_pop(L, 1);
    lua_pushcfunction(L, libs->open_func); /* 3 */
    lua_pushvalue(L, -2); /* 2->4 */
    lua_call(L, 1, 1); /* 3,4->3 */
    lua_rawset(L, -5); /* 2,3->1 */
  }
  lua_pop(L, 1);
}

void lbind_requireinto(lua_State *L, const char *prefix, lbind_Reg *libs) {
  /* stack: table */
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED"); /* 1 */
  for (; libs->name != NULL; ++libs) {
    lua_pushstring(L, libs->name); /* 2 */
    if (prefix == NULL)
      lua_pushvalue(L, -1); /* 3 */
    else
      lua_pushfstring(L, "%s.%s", prefix, libs->name); /* 3 */
    lua_pushvalue(L, -1); /* 4 */
    lua_rawget(L, -4); /* 4->4 */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1); /* (4) */
      lua_pushcfunction(L, libs->open_func); /* 4 */
      lua_pushvalue(L, -2); /* 3->5 */
      lua_call(L, 1, 1); /* 4,5->4 */
      lua_pushvalue(L, -2); /* 3->5 */
      lua_pushvalue(L, -2); /* 4->6 */
      /* stack: table [_LOADED name prefix.name ret prefix.name ret] */
      lua_rawset(L, -6); /* 4,5->1 */
    }
    lua_remove(L, -2); /* (3) */
    lua_rawset(L, -4); /* 2,3->table */
  }
  lua_pop(L, 1);
}


/* lbind error maintain */

int lbind_typeerror(lua_State *L, int idx, const char *tname) {
  const char *real_type = lbind_type(L, idx);
  const char *msg;
  if (real_type == NULL)
    real_type = luaL_typename(L, idx);
  msg = lua_pushfstring(L, "%s expected, got %s",
      tname, real_type);
  return luaL_argerror(L, idx, msg);
}

int lbind_matcherror(lua_State *L, const char *extramsg) {
  lua_Debug ar;
  lua_getinfo(L, "n", &ar);
  if (ar.name == NULL)
    ar.name = "?";
  return luaL_error(L, "no matching functions for call to %s\n"
      "candidates are:\n%s", ar.name, extramsg);
}

static int Ltraceback(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg)
        luaL_traceback(L, L, msg, 1);
    else if (!lua_isnoneornil(L, 1)) {  /* is there an error object? */
        if (!luaL_callmeta(L, 1, "__tostring"))  /* try its 'tostring' metamethod */
            lua_pushliteral(L, "(no error message)");
    }
    return 1;
}

int lbind_self(lua_State *L, const void *p, const char *method, int nargs, int *ptraceback) {
  luaL_checkstack(L, nargs+3, "too many arguments to self call");
  if (!lbind_retrieve(L, p)) return 0; /* 1 */
  lua_getfield(L, -1, method); /* 2 */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  if (ptraceback) {
    lua_pushcfunction(L, Ltraceback);
    lua_insert(L, -3);
    *ptraceback = lua_gettop(L) - 3;
  }
  lua_insert(L, -2);
  /* stack: traceback method object */
  return 1;
}


/* lbind information hash routine */

/* @@ hash algorithm @@
 * -- run "update_hash.lua" to update hash values
 * local input = ...
 * local hexnum = "0123456789ABCDEF"
 * local r = "1B"
 * for i = 1, #input do
 *   local ch = input:sub(i,i):upper()
 *   if hexnum:match(ch) then
 *     r = r .. ch
 *   elseif ch:match "%a" then
 *     local n = (ch:byte() - ("G"):byte())%16+1
 *     r = r .. hexnum:sub(n,n)
 *   end
 * end
 * return r
 * @@ end of hash algorithm @@
 */
#define LBIND_HASHS(X) \
  X(S, basetable, "__bases",    /* bsetbl */ 0x1BBCEDB5) \
  X(S, libtable,  "__methods",  /* libtbl */ 0x1B52BDB5) \
  X(S, typeinfo,  "__typeinfo", /* typeif */ 0x1BD29E2F) \
  X(S, getter,    "__get",      /* gettbl */ 0x1B0EDDB5) \
  X(S, setter,    "__set",      /* settbl */ 0x1BCEDDB5) \
  X(S, geti,      "__geti",     /* getifc */ 0x1B0ED2FC) \
  X(S, seti,      "__seti",     /* setifc */ 0x1BCED2FC) \
  X(S, new,       "new",        /* newnex */ 0x1B7E07E1) \
  X(S, delete,    "delete",     /* deletx */ 0x1BDE5ED1) \
  X(S, new_local, "new_local",  /* newlcl */ 0x1B7E05C5) \
  X(T, ptrbox,    "ptrbox",     /* ptrbox */ 0x1B9DBB81) \
  X(T, typebox,   "typebox",    /* typinf */ 0x1BD2927F) \
  X(T, libbox,    "libbox",     /* libbox */ 0x1B52BB81) \
  X(T, libmeta,   "libmata",    /* libmex */ 0x1B52B6E1) \
  X(T, enummeta,  "enummeta",   /* enumex */ 0x1BE7E6E1)

typedef enum lbind_Hash {
#define X(T,name,value,hash) lbH_##name = hash,
  LBIND_HASHS(X)
#undef  X
  lbH_Count
} lbind_Hash;

static void init_strings(lua_State *L) {
#define S(value,hash) \
  lua_pushstring(L, value); \
  lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)hash);
#define T(value,hash)
#define X(T,name,value,hash) T(value,hash)
  LBIND_HASHS(X)
#undef  X
#undef  S
#undef  T
}

static void lbH_rawgets(lua_State *L, int idx, lbind_Hash h) {
  int retried = 0;
retry:
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)h);
  if (!lua_isnil(L, -1))
    lua_rawget(L, relate_index(idx, 1));
  else {
    lua_pop(L, 1);
    init_strings(L);
    assert(retried == 0);
    retried = 1;
    goto retry;
  }
}

static void lbH_rawsets(lua_State *L, int idx, lbind_Hash h) {
  int retried = 0;
retry:
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)h);
  if (!lua_isnil(L, -1)) {
    lua_insert(L, -2);
    lua_rawset(L, relate_index(idx, 1));
  }
  else {
    lua_pop(L, 1);
    init_strings(L);
    assert(retried == 0);
    retried = 1;
    goto retry;
  }
}

static void setnewtable(lua_State *L, lbind_Hash h, const char *config) {
  lua_newtable(L); /* 1 */
  if (config != NULL) {
    lua_newtable(L); /* 2 */
    lua_pushliteral(L, "__mode"); /* 3 */
    lua_pushstring(L, config); /* 4 */
    lua_rawset(L, -3); /* 3,4->2 */
    lua_setmetatable(L, -2); /* 2->1 */
  }
  lua_pushvalue(L, -1); /* 1->2 */
  lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)h); /* 2->env */
}

static void lbH_getpointerbox(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)lbH_ptrbox);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    setnewtable(L, lbH_ptrbox, "v");
  }
}

static void lbH_gettypemap(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)lbH_typebox);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    setnewtable(L, lbH_typebox, "v");
  }
}

static void lbH_getlibmap(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)lbH_libbox);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    setnewtable(L, lbH_libbox, "v");
  }
}

static int Llibcall(lua_State *L) {
  lbH_rawgets(L, 1, lbH_new_local);
  if (lua_isnil(L, -1))
    lbH_rawgets(L, 1, lbH_new);
  if (lua_isnil(L, -1))
    return luaL_argerror(L, 1, "no 'new_local' or 'new' method found");
  lua_replace(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

static void lbH_getlibmeta(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)lbH_libmeta);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    setnewtable(L, lbH_libmeta, NULL);
    lua_pushliteral(L, "__call"); /* 2 */
    lua_pushcfunction(L, Llibcall); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
  }
}

static int Lenumcall(lua_State *L) {
  lbind_Enum *et;
  lbH_getlibmeta(L); /* 1 */
  lua_pushvalue(L, 1); /* 2 */
  lua_rawget(L, -2); /* 2->2 */
  if ((et = (lbind_Enum*)lua_touserdata(L, -1)) == NULL)
    return 0;
  lua_pushinteger(L, lbind_checkmask(L, 2, et));
  return 1;
}

static void lbH_getenummeta(lua_State *L) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)lbH_enummeta);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    setnewtable(L, lbH_enummeta, NULL);
    lua_pushliteral(L, "__call"); /* 2 */
    lua_pushcfunction(L, Lenumcall); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
  }
}


/* library metatable */

static int Llibindex(lua_State *L) {
  if (lua_getmetatable(L, 1)) {
    if (lua_type(L, 2) == LUA_TNUMBER) {
      lua_CFunction geti;
      lbH_rawgets(L, -1, lbH_geti);
      geti = lua_tocfunction(L, -1);
      lua_settop(L, 2);
      if (geti != NULL)
        return geti(L);
    }
    else {
      lua_CFunction getter;
      lbH_rawgets(L, -1, lbH_getter);
      getter = lua_tocfunction(L, -1);
      if (getter == NULL) {
        lua_pushvalue(L, 2); /* 3 */
        lua_rawget(L, -2); /* 3->3 */
        getter = lua_tocfunction(L, -1);
      }
      lua_settop(L, 2);
      if (getter != NULL)
        return getter(L);
    }
  }
  return 0;
}

int lbind_newlibmeta(lua_State *L, int idx) {
  if (lua_getmetatable(L, idx)) {
    lbH_getlibmeta(L);
    if (lua_rawequal(L, -1, -2)) {
      lua_newtable(L);
      lua_pushcfunction(L, Llibcall);
      lua_setfield(L, -2, "__call");
      lua_replace(L, -3);
    }
    lua_pop(L, 1);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, relate_index(idx, 2));
    return 1;
  }
  return 0;
}

void lbind_setlibgetters(lua_State *L, int idx, luaL_Reg *getters) {
  if (lbind_newlibmeta(L, idx)) {
    lua_newtable(L);
    luaL_setfuncs(L, getters, 0);
    lbH_rawsets(L, -2, lbH_getter);
    lua_pushcfunction(L, Llibindex);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
  }
}

void lbind_setlibgetter(lua_State *L, int idx, lua_CFunction getter) {
  if (lbind_newlibmeta(L, idx)) {
    lua_pushcfunction(L, getter);
    lbH_rawsets(L, -2, lbH_getter);
    lua_pushcfunction(L, Llibindex);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
  }
}

void lbind_setlibarrayf(lua_State *L, int idx, lua_CFunction geti) {
  if (lbind_newlibmeta(L, idx)) {
    lua_pushcfunction(L, geti);
    lbH_rawsets(L, -2, lbH_geti);
    lua_pushcfunction(L, Llibindex);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
  }
}


/* lbind userdata maintain */

typedef union {
  lbind_MaxAlign dummy; /* ensures maximum alignment for `intern' object */
  struct {
    void *instance;
    int flags;
  } o;
} lbind_Object;

#define LBC_GC          0x01
#define LBC_HASSETTER   0x02
#define LBC_HASGETTER   0x04
#define LBC_HASSETI     0x08
#define LBC_HASGETI     0x10

#define check_size(L,n) (lua_rawlen((L),(n)) >= sizeof(lbind_Object))

static lbind_Object *raw_newobj(lua_State *L, size_t objsize, int flags) {
  lbind_Object *obj;
  lbH_getpointerbox(L); /* 1 */
  obj = (lbind_Object*)lua_newuserdata(L, sizeof(lbind_Object) + objsize); /* 2 */
  obj->o.instance = (void*)(obj+1);
  obj->o.flags = flags;
  lua_pushvalue(L, -1); /* 2->3 */
  lua_rawsetp(L, -3, obj->o.instance); /* 3->1 */
  lua_remove(L, -2); /* (1) */
  return obj;
}

static lbind_Object *ptr_newobj(lua_State *L, const void *p, int flags) {
  lbind_Object *obj;
  lbH_getpointerbox(L); /* 1 */
  obj = (lbind_Object*)lua_newuserdata(L, sizeof(lbind_Object)); /* 2 */
  obj->o.instance = (void*)p;
  obj->o.flags = flags;
  lua_pushvalue(L, -1); /* 2->3 */
  lua_rawsetp(L, -3, obj->o.instance); /* 3->1 */
  lua_remove(L, -2); /* (1) */
  return obj;
}

static lbind_Object *to_object(lua_State *L, int idx) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  if (obj != NULL) {
    if (!check_size(L, idx) || obj->o.instance == NULL)
      obj = NULL;
    else {
      lbH_getpointerbox(L); /* 1 */
      lua_rawgetp(L, -1, obj->o.instance); /* 2 */
      if (!lua_rawequal(L, relate_index(idx, 2), -1))
        obj = NULL;
      lua_pop(L, 2); /* (2)(1) */
    }
  }
  return obj;
}

void lbind_track(lua_State *L, int idx) {
  lbind_Object *obj = to_object(L, idx);
  if (obj != NULL)
    obj->o.flags |= LBC_GC;
}

void lbind_untrack(lua_State *L, int idx) {
  lbind_Object *obj = to_object(L, idx);
  if (obj != NULL)
    obj->o.flags &= ~LBC_GC;
}

int lbind_hastrack(lua_State *L, int idx) {
  lbind_Object *obj = to_object(L, idx);
  return obj != NULL && (obj->o.flags & LBC_GC) != 0;
}

void *lbind_object(lua_State *L, int idx) {
  lbind_Object *obj = to_object(L, idx);
  return obj == NULL ? NULL : obj->o.instance;
}

int lbind_retrieve(lua_State *L, const void *p) {
  lbH_getpointerbox(L); /* 1 */
  lua_rawgetp(L, -1, p); /* 2 */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  lua_remove(L, -2);
  return 1;
}

void *lbind_unregister(lua_State *L, int idx) {
  void *u = NULL;
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  if (obj != NULL) {
    if (!check_size(L, idx))
      return NULL;
    if ((u = obj->o.instance) != NULL) {
      obj->o.instance = NULL;
      obj->o.flags &= ~LBC_GC;
#if LUA_VERSION_NUM < 502
      lbH_getpointerbox(L); /* 1 */
      lua_pushnil(L); /* 2 */
      lua_rawsetp(L, -3, u); /* 2->1 */
      lua_pop(L, 1); /* (1) */
#endif
    }
  }
  return u;
}


/* lbind class maintain */

int lbind_getmetatable(lua_State *L, const lbind_Type *t) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (const void*)t);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

int lbind_getlibtable(lua_State *L, const lbind_Type *t) {
  if (lbind_getmetatable(L, t)) return 0;
  lbH_rawgets(L, -1, lbH_libtable);
  lua_remove(L, -2);
  assert(!lua_isnil(L, -1));
  return 1;
}

void lbind_inittype(lua_State *L, const char *name, lbind_Type **bases, lbind_Type *t) {
  t->name = name;
  t->flags = LBC_GC; /* autotrack default */
  t->bases = bases;
  t->cast = NULL;
}

static int Lnewlocal(lua_State *L) {
  lbH_rawgets(L, lua_upvalueindex(1), lbH_new);
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  lbind_track(L, 1);
  return lua_gettop(L);
}

static int Ltostring(lua_State *L) {
  lbind_tolstring(L, 1, NULL);
  return 1;
}

static int Lgc(lua_State *L) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
  if (obj != NULL && check_size(L, 1)) {
    if ((obj->o.flags & LBC_GC) != 0) {
      lua_getfield(L, 1, "delete");
      if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 0);
      }
      if ((obj->o.flags & LBC_GC) != 0)
        lbind_unregister(L, 1);
    }
  }
  return 0;
}

static int Lnewindex(lua_State *L) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
  if (obj != NULL && lua_getmetatable(L, 1)) { /* 1 */
    if ((obj->o.flags & LBC_HASSETI) != 0
        && lua_type(L, 2) == LUA_TNUMBER) {
      lua_CFunction seti;
      lbH_rawgets(L, -1, lbH_seti); /* 2 */
      seti = lua_tocfunction(L, -1);
      lua_settop(L, 2);
      if (seti != NULL)
        return seti(L);
    }
    else if ((obj->o.flags & LBC_HASSETTER) != 0) {
      lua_CFunction setter;
      /* setter table */
      lbH_rawgets(L, -1, lbH_setter); /* 2 */
      setter = lua_tocfunction(L, -1);
      if (setter == NULL) {
        lua_pushvalue(L, 2); /* 3 */
        lua_rawget(L, -2); /* 3->3 */
        setter = lua_tocfunction(L, -1);
      }
      lua_settop(L, 3);
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

static int Lindex(lua_State *L) {
  int i;
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, 1);
  if (obj != NULL) {
    /* find in uservalue table */
    lua_getuservalue(L, 1);
    if (!lua_isnil(L, -1)) {
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1))
        return 1;
    }
    if (lua_getmetatable(L, 1)) { /* 1 */
      if ((obj->o.flags & LBC_HASGETI) != 0
          && lua_type(L, 2) == LUA_TNUMBER) {
        lua_CFunction geti;
        lbH_rawgets(L, -1, lbH_geti); /* 2 */
        geti = lua_tocfunction(L, -1);
        lua_settop(L, 2);
        if (geti != NULL)
          return geti(L);
      }
      else if ((obj->o.flags & LBC_HASGETTER) != 0) {
        lua_CFunction getter;
        /* getter table */
        lbH_rawgets(L, -1, lbH_getter); /* 2 */
        getter = lua_tocfunction(L, -1);
        if (getter == NULL) {
          lua_pushvalue(L, 2); /* 3 */
          lua_rawget(L, -2); /* 3->3 */
          getter = lua_tocfunction(L, -1);
        }
        lua_settop(L, 2);
        if (getter != NULL)
          return getter(L);
      }
    }
  }
  /* find in libtable/superlibtable */
  for (i = 1; !lua_isnone(L, lua_upvalueindex(i)); ++i) {
    lua_settop(L, 2);
    if (lua_islightuserdata(L, lua_upvalueindex(i))) {
      lbind_getmetatable(L, (const lbind_Type*)lua_touserdata(L, lua_upvalueindex(i)));
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        continue;
      }
      lbH_rawgets(L, -1, lbH_libtable);
      lua_replace(L, lua_upvalueindex(i));
    }
    lua_pushvalue(L, 2);
    lua_gettable(L, lua_upvalueindex(i));
    if (!lua_isnil(L, -1))
      return 1;
  }
  return 0;
}

static void push_indexfunc(lua_State *L, lbind_Type **bases) {
  int nups = 1; /* stack: libtable */
  if (bases != NULL) {
    for (; *bases != NULL; ++nups, ++bases) {
      luaL_checkstack(L, 2, "no space for base types");
      lbind_getmetatable(L, *bases);
      if (lua_isnil(L, -1))
        lua_pushlightuserdata(L, *bases);
      else
        lbH_rawgets(L, -1, lbH_libtable);
      lua_remove(L, -2);
    }
  }
  lua_pushcclosure(L, Lindex, nups);
}

static void set_default(lua_State *L, int idx, const char *key) {
  /* stack: libtable mt */
  lua_getfield(L, idx, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_setfield(L, idx, key);
  }
  else
    lua_pop(L, 2);
}

static int Lagency(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_gettable(L, 1);
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L)-1, LUA_MULTRET);
  return lua_gettop(L);
}

static void set_meta_agency(lua_State *L, const char *key) {
  /* stack: libtable mt */
  lua_pushstring(L, key);
  lua_rawget(L, -3); /* do not track __index */
  if (!lua_isnil(L, -1)) {
    lua_pushfstring(L, "__%s", key);
    lua_pushstring(L, key);
    lua_pushcclosure(L, Lagency, 1);
    lua_rawset(L, -4);
  }
  lua_pop(L, 1);
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
  lbH_gettypemap(L); /* 1 */
  lua_pushstring(L, tname); /* 2 */
  lua_pushlightuserdata(L, t); /* 3 */
  lua_rawset(L, -3); /* 2,3->1 */
  lua_pop(L, 1); /* (1) */
  lbH_getlibmap(L); /* 1 */
  lua_pushvalue(L, -2); /* libtable->2 */
  lua_pushlightuserdata(L, t); /* 3 */
  lua_rawset(L, -3); /* 2,3->1 */
  lua_pop(L, 2); /* (1)(libtable) */
}

void lbind_setmt(lua_State *L, lbind_Type *t) {
  /* stack: libtable mt */

  /* add new_local function */
  lbH_rawgets(L, -2, lbH_new); /* 1 */
  lbH_rawgets(L, -3, lbH_new_local); /* 2 */
  if (!lua_isnil(L, -2) && lua_isnil(L, -1)) {
    lua_pushvalue(L, -4); /* libtable->3 */
    lua_pushcclosure(L, Lnewlocal, 1); /* 3->3 */
    lbH_rawsets(L, -5, lbH_new_local); /* 3->libtable */
  }
  lua_pop(L, 2);

  /* set metatable to libtable */
  lbH_getlibmeta(L); /* 1 */
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
  set_meta_agency(L, "len");
#if LUA_VERSION_NUM >= 502
  set_meta_agency(L, "pairs");
  set_meta_agency(L, "ipairs");
#endif /* LUA_VERSION_NUM >= 502 */

  /* set typeinfo for metatable */
  lua_pushliteral(L, "class"); /* 1 */
  lua_rawsetp(L, -2, t); /* 1->mt */
  lua_pushvalue(L, -2); /* libtable->1 */
  lbH_rawsets(L, -2, lbH_libtable); /* 1->mt */
  lua_pushlightuserdata(L, (void*)t); /* 1 */
  lbH_rawsets(L, -2, lbH_typeinfo); /* 1->mt */

  /* set global informations */
  lua_pushvalue(L, -2); /* libtable->1 */
  register_global_info(L, t->name, t); /* (1) */

  /* set metatable to registry */
  lua_rawsetp(L, LUA_REGISTRYINDEX, t); /* (mt) */
}

void lbind_setcast(lua_State *L, lbind_Cast *cast, lbind_Type *t) {
  t->cast = cast;
}

int lbind_setautotrack(lua_State *L, int autotrack, lbind_Type *t) {
  int old_flag = t->flags&LBC_GC ? 1 : 0;
  if (autotrack)
    t->flags |= LBC_GC;
  else
    t->flags &= ~LBC_GC;
  return old_flag;
}

void lbind_setaccessor(lua_State *L, luaL_Reg *getters, luaL_Reg *setters, lbind_Type *t) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, t);
  if (getters) {
    lua_newtable(L);
    luaL_setfuncs(L, getters, 0);
    lbH_rawsets(L, -2, lbH_getter);
    t->flags |= LBC_HASGETTER;
  }
  if (setters) {
    lua_newtable(L);
    luaL_setfuncs(L, setters, 0);
    lbH_rawsets(L, -2, lbH_setter);
    t->flags |= LBC_HASSETTER;
  }
  lua_pop(L, 1);
}

void lbind_sethashf(lua_State *L, lua_CFunction getter, lua_CFunction setter, lbind_Type *t) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, t);
  if (getter) {
    lua_pushcfunction(L, getter);
    lbH_rawsets(L, -2, lbH_getter);
    t->flags |= LBC_HASGETTER;
  }
  if (setter) {
    lua_pushcfunction(L, setter);
    lbH_rawsets(L, -2, lbH_setter);
    t->flags |= LBC_HASSETTER;
  }
  lua_pop(L, 1);
}

void lbind_setarrayf(lua_State *L, lua_CFunction geti, lua_CFunction seti, lbind_Type *t) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, t);
  if (geti) {
    lua_pushcfunction(L, geti);
    lbH_rawsets(L, -2, lbH_geti);
    t->flags |= LBC_HASGETI;
  }
  if (seti) {
    lua_pushcfunction(L, seti);
    lbH_rawsets(L, -2, lbH_seti);
    t->flags |= LBC_HASSETI;
  }
  lua_pop(L, 1);
}


/* lbind type system */

static int testudata(lua_State *L, int idx, const lbind_Type *t) {
  if (lua_getmetatable(L, idx) /* does it have a metatable? */
      && lbind_getmetatable(L, t)) { /* get correct metatable */
    if (!lua_rawequal(L, -1, -2))  /* not the same? */
      return 0;  /* value is a userdata with wrong metatable */
    lua_pop(L, 2);  /* remove both metatables */
    return 1;
  }
  return 0;
}

const char *lbind_type(lua_State *L, int idx) {
  if (lua_getmetatable(L, idx)) { /* 1 */
    const lbind_Type *t = NULL;
    lbH_rawgets(L, -1, lbH_typeinfo); /* 2 */
    if (!lua_isnil(L, -1))
      t = (const lbind_Type*)lua_touserdata(L, -1);
    lua_pop(L, 2); /* (2)(1) */
    if (t != NULL)
      return t->name;
  }
  return NULL;
}

void *try_cast(lua_State *L, int idx, const lbind_Type *t) {
  if (lua_getmetatable(L, idx)) { /* 1 */
    lbind_Type *from_type;
    lbH_rawgets(L, -1, lbH_typeinfo); /* 2 */
    from_type = (lbind_Type*)lua_touserdata(L, -1);
    lua_pop(L, 2); /* (2)(1) */
    if (from_type != NULL && from_type->cast != NULL)
      return from_type->cast(L, idx, t);
  }
  return NULL;
}

int lbind_isa(lua_State *L, int idx, const lbind_Type *t) {
  return testudata(L, idx, t) || try_cast(L, idx, t) != NULL;
}

void *lbind_cast(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  if (!check_size(L, idx) || obj == NULL || obj->o.instance == NULL)
    return NULL;
  return testudata(L, idx, t) ? obj->o.instance : try_cast(L, idx, t);
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

void *lbind_check(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  void *u = NULL;
  if (!check_size(L, idx))
    luaL_argerror(L, idx, "invalid lbind userdata");
  if (obj != NULL && obj->o.instance == NULL)
    luaL_argerror(L, idx, "null lbind object");
  u = testudata(L, idx, t) ? obj->o.instance : try_cast(L, idx, t);
  if (u == NULL)
    lbind_typeerror(L, idx, t->name);
  return u;
}

void *lbind_test(lua_State *L, int idx, const lbind_Type *t) {
  lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
  return testudata(L, idx, t) ? obj->o.instance : try_cast(L, idx, t);
}


/* lbind object maintain */

const char *lbind_tolstring(lua_State *L, int idx, size_t *plen) {
  const char *tname = lbind_type(L, idx);
  lbind_Object *obj = to_object(L, idx);
  if (obj != NULL && tname)
    lua_pushfstring(L, "%s: %p", tname, obj->o.instance);
  else if (obj == NULL) {
    lbind_Object *obj = (lbind_Object*)lua_touserdata(L, idx);
    if (obj == NULL)
      return luaL_tolstring(L, idx, plen);
    if (check_size(L, 1))
      lua_pushfstring(L, "%s[N]: %p", tname, obj->o.instance);
    else
      lua_pushfstring(L, "userdata: %p", (void*)obj);
  }
  return lua_tolstring(L, -1, plen);
}

void *lbind_new(lua_State *L, size_t objsize, const lbind_Type *t) {
  void *p = raw_newobj(L, objsize, t->flags)->o.instance;
  if (lbind_getmetatable(L, t))
    lua_setmetatable(L, -2);
  return p;
}

void *lbind_raw(lua_State *L, size_t objsize) {
  return raw_newobj(L, objsize, 0)->o.instance;
}

void lbind_register(lua_State *L, const void *p, const lbind_Type *t) {
  ptr_newobj(L, p, t->flags);
  if (lbind_getmetatable(L, t))
    lua_setmetatable(L, -2);
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
    valid = (*value = (int)lua_tonumber(L, -1)) != 0 || lua_isnumber(L, -1);
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

static int parse_mask(lua_State *L, const char *s, int *penum, int check) {
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

int lbind_getenumtable (lua_State *L, const lbind_Enum *et) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, (const void*)et);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

int lbind_pushmask(lua_State *L, int evalue, lbind_Enum *et) {
  lua_pushinteger(L, evalue);
  return evalue;
}

int lbind_pushenum(lua_State *L, const char *name, lbind_Enum *et) {
  int res;
  lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
  lua_pushstring(L, name); /* 2 */
  lua_pushvalue(L, -1); /* 2->3 */
  lua_rawget(L, -3); /* 3->3 */
  lua_remove(L, -2); /* (1) */
  if ((res = (int)lua_tonumber(L, -1)) == 0 && !lua_isnumber(L, -1)) {
    lua_pop(L, 1); /* (3) */
    return -1;
  }
  lua_remove(L, -2); /* (2) */
  return res;
}

static int toenum(lua_State *L, int idx, lbind_Enum *et, int mask, int check) {
  const char *str;
  int value = lua_tointeger(L, idx);
  if (value != 0 || lua_isnumber(L, idx))
    return value;
  if ((str = lua_tostring(L, idx)) != NULL) {
    int success;
    lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
    if (mask)
      success = parse_mask(L, str, &value, check);
    else {
      lua_pushvalue(L, relate_index(idx, 1)); /* idx->2 */
      lua_rawget(L, -2); /* 2->2 */
      success = (value = (int)lua_tonumber(L, -1)) != 0 || lua_isnumber(L, -1);
      lua_pop(L, 1); /* (2) */
      if (check && !success)
        return luaL_error(L, "'%s' is not valid %s", str, et->name);
    }
    lua_pop(L, 1); /* (1) */
    if (success)
      return value;
  }
  if (check)
    lbind_typeerror(L, idx, et->name);
  return -1;
}

int lbind_testmask(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 1, 0);
}

int lbind_checkmask(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 1, 1);
}

int lbind_testenum(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 0, 0);
}

int lbind_checkenum(lua_State *L, int idx, lbind_Enum *et) {
  return toenum(L, idx, et, 0, 1);
}

int lbind_addenum(lua_State *L, int idx, lbind_Enum *et) {
  if (lua_isstring(L, idx)) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
    lua_pushvalue(L, relate_index(idx, 1)); /* 2 */
    lua_pushinteger(L, ++et->lastn); /* 3 */
    lua_rawset(L, -3); /* 2,3->1 */
    lua_pop(L, 1); /* (1) */
    return et->lastn;
  }
  return -1;
}

static void addenums(lua_State *L, lbind_EnumItem *enums, lbind_Enum *et) {
  /* stack: etable */
  for (; enums->name != NULL; ++enums) {
    lua_pushstring(L, enums->name); /* 1 */
    lua_pushinteger(L, enums->value); /* 2 */
    lua_rawset(L, -3); /* 1,2->etable */
    if (et->lastn < enums->value)
      et->lastn = enums->value;
  }
}

int lbind_addenums(lua_State *L, lbind_EnumItem *enums, lbind_Enum *et) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, et); /* 1 */
  addenums(L, enums, et);
  lua_pop(L, 1); /* (1) */
  return et->lastn;
}

void lbind_initenum(lua_State *L, const char *name, lbind_EnumItem *enums, lbind_Enum *et) {
  /* stack: etable */
  et->name = name;
  et->lastn = 0;
  et->enums = enums;

  /* add enums to etable */
  addenums(L, enums, et);

  /* set metatable */
  lbH_getenummeta(L); /* 1 */
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
  const void *u = lbind_object(L, -1);
  if (u != NULL)
    lua_pushlightuserdata(L, (void*)u);
  else
    lua_pushnil(L);
  return 1;
}

static int Lvalid(lua_State *L) {
  const void *u = lua_touserdata(L, -1);
  if (u != NULL) {
    lbH_getpointerbox(L);
    lua_rawgetp(L, -1, u);
  }
  else
    lua_pushnil(L);
  return 1;
}

static int Ldelete(lua_State *L) {
  lbH_rawgets(L, -1, lbH_delete);
  if (!lua_isnil(L, -1)) {
    lua_pushvalue(L, -2);
    lua_call(L, 1, 0);
  }
  return 0;
}

static int Linfo(lua_State *L) {
#define INFOS \
  X( 1, "pointers",     lbH_getpointerbox(L)) \
  X( 2, "types",        lbH_gettypemap(L)) \
  X( 3, "libmap",       lbH_getlibmap(L)) \
  X( 4, "libmeta",      lbH_getlibmeta(L)) \
  X( 5, "enummeta",     lbH_getenummeta(L)) \
  X( 6, "pointers_key", lua_pushlightuserdata(L, (void*)lbH_ptrbox)) \
  X( 7, "types_key",    lua_pushlightuserdata(L, (void*)lbH_typebox)) \
  X( 8, "libmap_key",   lua_pushlightuserdata(L, (void*)lbH_libbox)) \
  X( 9, "libmeta_key",  lua_pushlightuserdata(L, (void*)lbH_libmeta)) \
  X(10, "enummeta_key", lua_pushlightuserdata(L, (void*)lbH_enummeta))

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
      lbH_gettypemap(L); /* 1 */
    else
      lbH_getlibmap(L); /* 1 */
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
      lua_replace(L, -2); /* (ud) */
      lbH_rawgets(L, -1, lbH_typeinfo); /* 2 */
      lua_replace(L, -2); /* (1) */
      if (lua_islightuserdata(L, -1))
        return 1;
    }
    lua_pop(L, 1); /* (ud) */
  }
  else if (lua_istable(L, -1)) {
    lbH_getlibmap(L); /* 1 */
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
        type = t->name;
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
  lbH_rawgets(L, -1, lbH_libtable);
  return 1;
}

static int Lbases(lua_State *L) {
  lbind_Type *t;
  int incomplete = 0;
  if (!get_classmt(L)) return 0;
  lbH_rawgets(L, -1, lbH_basetable); /* 2 */
  if (!lua_isnil(L, -1)) {
    lua_pushliteral(L, "incomplete"); /* 3 */
    lua_rawget(L, -2); /* 3->3 */
    incomplete = lua_toboolean(L, -1);
    lua_pop(L, 1); /* (3) */
    if (!incomplete)
      return 1;
  }
  lbH_rawgets(L, -1, lbH_typeinfo); /* 3 */
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
          lbH_rawgets(L, -1, lbH_libtable); /* 4 */
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
    lbH_rawgets(L, -1, lbH_basetable); /* 4->3 */
    lua_pop(L, 1); /* (3) */
    return 1;
  }
  return 0;
}

static int Lmask(lua_State *L) {
  get_typeptr(L);
  if (lua_islightuserdata(L, -1)) {
    int value, first = 1;
    luaL_Buffer b;
    lbind_EnumItem *ei;
    lbind_Enum *et = (lbind_Enum*)lua_touserdata(L, -1);
    if (!is_type(L, et, 'e'))
      return 0;
    lua_pop(L, 1); /* pop enum table from is_type */
    value = lbind_checkmask(L, -2, et);
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
    ENTRY(mask),
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
 * cc: dep='ninja' lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-DLUA_BUILD_AS_DLL' output='lbind.dll'
 * cc: run='$lua tt.lua'
 */
