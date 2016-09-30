#include <stdio.h>
#include "lua.h"
#include "luauser.h"
#include "lauxlib.h"
#include "lstate.h"

#include <memory.h>
#include <stdlib.h>

//#define P_DEBUG

#ifdef __ANDROID__

#include <android/log.h>
#define LOG_TAG "lua.print"
#undef LOG
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG , LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR , LOG_TAG,__VA_ARGS__)

#else

#define LOGD(...) fprintf(stdout, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)

#endif

#ifndef GLOBAL_LOCK
#define STATEDATA_TOLOCK(L) (*((StateData **)lua_getextraspace(L))->lock
#else
pthread_mutex_t lock;
int lockInited = 0;
#define STATEDATA_TOLOCK(L) lock
#endif

lua_State* GetMainState(lua_State *L){
    if (L) {
        return L->l_G->mainthread;
    } else {
        return NULL;
    }
}

void LockMainState(lua_State *L){
    if (L) {
        pthread_mutex_lock(&STATEDATA_TOLOCK(L));
    }
}

void UnLockMainState(lua_State *L){
    if (L) {
        pthread_mutex_unlock(&STATEDATA_TOLOCK(L));
    }
}

void LuaLockInitial(lua_State * L){
    StateData **sd = (StateData **)lua_getextraspace(L);
    *sd = malloc(sizeof(StateData));
    (*sd)->count = 0;
    (*sd)->thread_count = 0;

#ifdef GLOBAL_LOCK
    if (!lockInited) {
#endif
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&STATEDATA_TOLOCK(L), &a);
        
        lockInited = 1;
#ifdef GLOBAL_LOCK
    }
#endif

#ifdef P_DEBUG
    LOGD("initialState 0x%08lX\n", (long)L);
#endif
}

void LuaLockInitialThread(lua_State * L, lua_State * co){
    StateData *sd = *((StateData **)lua_getextraspace(L));
    sd->thread_count = sd->thread_count + 1;

#ifdef P_DEBUG
    unsigned int thread_count = sd->thread_count;
    LOGD("initialThread 0x%08lX thread_count: %d -> %d\n", (long)co, thread_count - 1, thread_count);
#endif
}

void LuaLockFinalState(lua_State * L){
    UnLockMainState(L);
#ifndef GLOBAL_LOCK
    pthread_mutex_destroy(&STATEDATA_TOLOCK(L));
#endif
    StateData **sd = (StateData **)lua_getextraspace(L);
    free(*sd);
}

void LuaLockFinalThread(lua_State * L, lua_State * co){
    StateData *sd = *((StateData **)lua_getextraspace(L));
    sd->thread_count = sd->thread_count - 1;
#ifdef P_DEBUG
    unsigned int thread_count = sd->thread_count;
    LOGD("finalThread 0x%08lX thread_count: %d -> %d\n", (long)co, thread_count + 1, thread_count);
#endif
}

void incrRef(lua_State *L){
    StateData *sd = *((StateData **)lua_getextraspace(L));
    lua_lock(L);

#ifdef P_DEBUG
        lua_getfield(L, LUA_REGISTRYINDEX, "dataPath");
        if (lua_type(L, -1) == LUA_TSTRING) {
            LOGD("0x%08lX(ROOT)\t%s incrRefRoot %d -> %d\n", (long)L, lua_tostring(L, -1), sd->count, sd->count + 1);
        } else {
            LOGD("0x%08lX(ROOT)\t incrRefRoot %d -> %d\n", (long)L, sd->count, sd->count + 1);
        }
        lua_pop(L, 1);
#endif
        
    sd->count = sd->count + 1;
    lua_unlock(L);
}

void decrRef(lua_State *L){
    StateData *sd = *((StateData **)lua_getextraspace(L));
    lua_lock(L);

#ifdef P_DEBUG
        lua_getfield(L, LUA_REGISTRYINDEX, "dataPath");
        if (lua_type(L, -1) == LUA_TSTRING) {
            LOGD("0x%08lX(ROOT)\t%s decrRefRoot %d -> %d\n", (long)L, lua_tostring(L, -1), sd->count, sd->count - 1);
        } else {
            LOGD("0x%08lX(ROOT)\t decrRefRoot %d -> %d\n", (long)L, sd->count, sd->count - 1);
        }
        lua_pop(L, 1);
#endif
        
    sd->count = sd->count - 1;
    
    lua_unlock(L);
    
//    if (STATEDATA_TO_COUNT(L) == 1) {
//        lua_gc(ROOT, LUA_GCCOLLECT, 0);
//    }

    if (sd->count <= 0) {
        if (sd->thread_count > 0) {
            printf("dealloc on thread_count = %d\n", sd->thread_count);
        }
        lua_close(L->l_G->mainthread);
        printf("dealloc state\n");
    }
}

LUALIB_API int luaL_typerror (lua_State *L, int narg, const char *tname) {
  const char *msg = lua_pushfstring(L, "%s expected, got %s",
                                    tname, luaL_typename(L, narg));
  return luaL_argerror(L, narg, msg);
}