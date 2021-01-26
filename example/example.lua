require'jit'.off()

local clock = require 'clock'
local ffi = require 'ffi'
rawset(_G, 'ffi', ffi)
package.path = 'luapower/?.lua;'..package.path

local thread = require 'thread'
rawset(_G, 'thread', thread)

local pthread = require 'pthread'
rawset(_G, 'pthread', pthread)

local xtm = require 'xtm'
rawset(_G, 'xtm', xtm)

local module = {
	mod = ffi.new('struct module', {
		ev = thread.event(),
		tx_xtm = xtm.xtm_create(4096),
	}),
}

module.mod.callback = function(arg)
	if arg ~= 0 then
		arg = ffi.cast('struct message_t *', arg)
		print("cb:", arg.sync, clock.time()-arg.time)
	end
end

module.th = thread.new(function(mod_addr)
	local ffi = require 'ffi'
	local xtm = require 'xtm'
	local mod = ffi.cast('struct module *', require 'glue'.ptr(mod_addr))

	mod.xtm_tx = xtm.xtm_create(4096)
	mod.ev:set(1)

	ffi.cdef[[
		typedef struct {
			long s;
			long ns;
		} time_timespec;

		int time_clock_gettime(int clock_id, time_timespec *tp) asm("clock_gettime");
	]]

	local clock do
		local tt = ffi.new('time_timespec')
		clock = function()
			assert(ffi.C.time_clock_gettime(0, tt) == 0)
			return tonumber(tt.s)+tonumber(tt.ns)/1e9
		end
	end

	local msg_t = ffi.typeof('struct message_t')

	for i = 1, 1e3 do
		while not xtm.xtm_msg_probe(mod.xtm_tx) do
			print "waiting"
			ffi.C.usleep(1000)
		end

		-- print("sending", i)

		xtm.xtm_fun_dispatch(mod.xtm_tx,
			mod.callback,
			ffi.cast('void *', msg_t(i, clock())),
			0)
	end

	print("finished")
	return 0
end, require'glue'.addr(ffi.cast('void *', module.mod)))

ffi.cdef[[
	static const int COIO_READ = 0x01;
	static const int COIO_WRITE = 0x02;

	int coio_wait(int fd, int event, double timeout);
]]

module.tx_fiber = require'fiber'.create(function(mod)
	mod.ev:wait()
	local in_fd = xtm.xtm_fd(mod.xtm_tx)
	for _ = 1, 1e3 do
		assert(ffi.C.coio_wait(in_fd, ffi.C.COIO_READ, math.huge) == ffi.C.COIO_READ, "coio_wait")
		print("run fun_invoke")
		assert(xtm.xtm_fun_invoke_all(mod.xtm_tx))
	end
end, module.mod)

rawset(_G, 'module', module)
require 'console'.start()

module.th:join()
