#include "../xtm_api.h"
#include <time.h>

#define _GNU_SOURCE
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <math.h>
#include "tarantool/lua.h"
#include "tarantool/lauxlib.h"
#include "tarantool/module.h"

struct ppq_t {
	struct xtm_queue *tx2net;
	struct xtm_queue *net2tx;

	pthread_t net_thread;
	lua_State *L;

	int is_running;
};

struct ppq_message_t {
	struct ppq_t *q;
	uint64_t id;
	uint64_t otime;
	uint64_t ctime;
};

static uint64_t now() {
	static struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec*1e9 + ts.tv_nsec;
}

static struct ppq_message_t* newmsg(uint64_t id) {
	struct ppq_message_t *msg = malloc(sizeof(struct ppq_message_t));
	msg->id = id;
	msg->otime = msg->ctime = now();
	return msg;
}

static void tx_consumer_func(void *);

static void *net_worker_f(void *arg) {
	struct ppq_t *q = (struct ppq_t *) arg;

	pthread_setname_np(pthread_self(), "xtmnet");

	q->tx2net = xtm_create(1024);
	int tx2net_fd = xtm_fd(q->tx2net);

	fprintf(stderr, "[net] xtm created tx2net_fd: %d\n", tx2net_fd);
	__atomic_store_n(&q->is_running, 1, __ATOMIC_RELEASE);

	while (__atomic_load_n(&q->is_running, __ATOMIC_ACQUIRE) == 1) {
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(tx2net_fd, &readset);

		int rc = select(tx2net_fd+1, &readset, NULL, NULL, NULL);
		if (rc < 0) {
			fprintf(stderr, "[net] select: %s", strerror(errno));
			continue;
		}

		if (FD_ISSET(tx2net_fd, &readset)) {
			assert(xtm_fun_invoke_all(q->tx2net) == 0);
		}
	}

	fprintf(stderr, "[net] Leaving\n");
	return NULL;
}

static void net_consumer_func(void *arg) {
	struct ppq_message_t *msg = (struct ppq_message_t*) arg;
	// fprintf(stderr, "[net] id: %lu; lat: %.4fs; otime: %.4fs\n", msg->id, now()-msg->ctime, msg->otime);

	struct ppq_t *q = msg->q;
	msg->ctime = now();
	int net2tx_fd = xtm_fd(q->net2tx);

	// Reply back:
	while (xtm_msg_probe(q->net2tx) != 0) {
		fd_set wset;
		FD_ZERO(&wset);
		FD_SET(net2tx_fd, &wset);

		int rc = select(net2tx_fd+1, NULL, &wset, NULL, NULL);
	}
	assert(xtm_fun_dispatch(q->net2tx, tx_consumer_func, msg, 0) == 0);
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

static void tx_consumer_func(void *arg) {
	struct ppq_message_t *msg = (struct ppq_message_t*) arg;
	// fprintf(stderr, "[tx] msg: %ld lat: %.4fs otime: %.4fs\n", msg->id, now()-msg->ctime, msg->otime);

	struct ppq_t *q = msg->q;
	double received_at = now();

	if (lua_gettop(q->L) > 1) {
		lua_pop(q->L, lua_gettop(q->L)-1);
	}
	assert(lua_gettop(q->L) == 1);

	// module table:
	luaL_checktype(q->L, 1, LUA_TTABLE);
	lua_pushstring(q->L, "on_ctx");
	lua_gettable(q->L, -2); // t.on_ctx

	if (lua_isfunction(q->L, -1)) {
		lua_pushvalue(q->L, 1); // arg[1] = L[1] (module table)
		lua_createtable(q->L, 0, 7); // arg[2] = x = {} (message table)

		lua_pushstring(q->L, "id");
		lua_pushinteger(q->L, msg->id);
		lua_settable(q->L, -3); // x.id = msg->id

		lua_pushstring(q->L, "ack");
		lua_pushnumber(q->L, (received_at-msg->ctime)/1e3);
		lua_settable(q->L, -3);

		lua_pushstring(q->L, "syn");
		lua_pushnumber(q->L, (msg->ctime-msg->otime)/1e3);
		lua_settable(q->L, -3);

		lua_pushstring(q->L, "rtt");
		lua_pushnumber(q->L, (received_at-msg->otime)/1e3);
		lua_settable(q->L, -3);

		lua_pushstring(q->L, "otime");
		lua_pushnumber(q->L, msg->otime);
		lua_settable(q->L, -3);

		lua_pushstring(q->L, "ftime");
		lua_pushnumber(q->L, received_at);
		lua_settable(q->L, -3);

		lua_pushstring(q->L, "rtime");
		lua_pushnumber(q->L, msg->ctime);
		lua_settable(q->L, -3);

		int r = lua_pcall(q->L, 2, 0, 0); // executes pcall, passes 2 arguments, receives zero
		if (r != 0) {
			fprintf(stderr, "[tx] on_ctx failed: %d\n", r);
		}
	}

	free(msg);
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

	double ctime = now();
	struct ppq_message_t *msg = newmsg((uint64_t) msg_id);
	msg->q = q;

	// fprintf(stderr, "[tx] send id: %lu otime: %.4fs\n", msg->id, msg->otime);

	int tx2net_fd = xtm_fd(q->tx2net);
	while(xtm_msg_probe(q->tx2net) != 0) {
		if ((coio_wait(tx2net_fd, COIO_WRITE, DBL_MAX) & COIO_WRITE) == 0) {
			luaL_error(L, "coio_wait failed");
		}
	}
	assert(xtm_fun_dispatch(q->tx2net, net_consumer_func, msg, 0) == 0);

	lua_pushnumber(L, now() - ctime);
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

	q->L = L;

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
