#include "../xtm_api.h"
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include "lua.h"
#include "lauxlib.h"
#include "module.h"

struct ppq_t {
	struct xtm_queue *tx2net;
	struct xtm_queue *net2tx;

	pthread_t net_thread;

	int is_running;
};

struct ppq_message_t {
	uint64_t id;
	double ctime;
};

static struct ppq_message_t* newmsg(uint64_t id) {
	struct ppq_message_t *msg = malloc(sizeof(struct ppq_message_t));

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	msg->id = id;
	msg->ctime = now.tv_sec + now.tv_nsec/1e9;
	return msg;
}

static void *net_worker_f(void *arg) {
	struct ppq_t *q = (struct ppq_t *) arg;

	q->tx2net = xtm_create(1024);
	int tx2net_fd = xtm_fd(q->tx2net);

	__atomic_store_n(&q->is_running, 1, __ATOMIC_RELEASE);

	while (__atomic_load_n(&q->is_running, __ATOMIC_ACQUIRE) == 1) {
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(tx2net_fd, &readset);

		int rc = select(tx2net_fd+1, &readset, NULL, NULL, NULL);
		if (rc < 0) {
			continue;
		}

		if (FD_ISSET(tx2net_fd, &readset)) {
			assert(xtm_fun_invoke_all(q->net2tx) == 0);
		}
	}
	return NULL;
}

/**
*  Creates new ppq
*/
static int new(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);    // context of the callback

	lua_pushstring(L, "on_ctx");
	lua_gettable(L, -2); // t.on_ctx

	if (!lua_isfunction(L, -1)) { // type(t.on_ctx) == 'function' ?
		luaL_error(L, "ppq.new requires function for 'on_ctx' got: %s",
			lua_typename(L, lua_type(L, -1)));
	}
	lua_pop(L, 1); // pops t.on_ctx

	lua_pushstring(L, "q");
	struct ppq_t *ppq = (struct ppq_t *) lua_newuserdata(L, sizeof(struct ppq_t));
	if (!ppq) {
		luaL_error(L, "Not enough memory: %s", strerror(errno));
	}
	memset(ppq, 0, sizeof(*ppq));

	lua_settable(L, -3); // t.q = ppq

	luaL_getmetatable(L, "ppq.metatable");
	lua_setmetatable(L, -2);

	return 1;
}

/**
 * Pushes message to the queue
 */
static int send(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	double msg_id = luaL_checknumber(L, 2);

	lua_pushstring(L, "q");
	lua_gettable(L, 1); // get t[q];

	if (!lua_isuserdata(L, -1)) {
		luaL_error(L, "ppq object is not found at index 'q', got: %s", lua_typename(L, lua_type(L, -1)));
	}

	struct ppq_t *q = (struct ppq_t *)lua_touserdata(L, -1);
	if (q == NULL) {
		luaL_error(L, "ppq:run() lost q");
	}

	struct ppq_message_t *msg = newmsg((uint64_t) msg_id);

	// if (xtm_msg_probe(q->tx2net) == 0) {
	// 	assert(xtm_fun_dispatch(q->tx2net, net_consumer_func, msg, 0) == 0);
	// } else {
	// 	luaL_error(L, "ppq failed: %s", strerror(errno));
	// }

	lua_pushboolean(L, 1);
	return 1;
}

static int run(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);

	lua_pushstring(L, "q");
	lua_gettable(L, 1); // get t[q];
	struct ppq_t *q = (struct ppq_t *)lua_touserdata(L, -1);
	if (q == NULL) {
		luaL_error(L, "ppq:run() lost q");
	}

	// 1. Create xtm_queue
	q->net2tx = xtm_create(1024);
	if(pthread_create(&q->net_thread, NULL, net_worker_f, q) != 0) {
		luaL_error(L, "pthread_create failed: %s", strerror(errno));
	}

	// 2. Wait when worker thread initializes itself
	while (__atomic_load_n(&q->is_running, __ATOMIC_ACQUIRE) != 1);

	// 3. loop to listen events from network thread
	int net2tx_fd = xtm_fd(q->net2tx);
	while (__atomic_load_n(&q->is_running, __ATOMIC_SEQ_CST) == 1) {
		if ((coio_wait(net2tx_fd, COIO_READ, DBL_MAX) & COIO_READ) == 0) {
			luaL_error(L, "coio_wait unexpectedly failed");
		}
		if (xtm_fun_invoke_all(q->net2tx) != 0) {
			luaL_error(L, "xtm_fun_invoke_all unexpectedly failed: %s", strerror(errno));
		}
	}

	return 0;
}

static const struct luaL_Reg ppqlib_f[] = {
	{"new", new},
	{NULL, NULL},
};

static const struct luaL_Reg ppqlib_m [] = {
	{"send", send},
	{"run", run},
	{NULL, NULL},
};

int luaopen_ppq(lua_State *L) {
	luaL_newmetatable(L, "ppq.metatable");

	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3); // metatable.__index = metatable

	luaL_openlib(L, NULL, ppqlib_m, 0);
	luaL_openlib(L, "ppq", ppqlib_f, 0);
	return 1;
}
