local log = require 'log'

local once = true

local stat = {}
rawset(_G, 'stat', stat)

local chan = require 'fiber'.channel()

local ppq = require 'ppq'.new {
	on_ctx = function(self, msg)
		-- log.info("lua: id: %d; rtt: %.4f mcs; syn: %.4f mcs (%.2f%%), ack: %.4f mcs (%.2f%%)",
		-- 	msg.id, msg.rtt,
		-- 	(msg.rtime-msg.otime)/1e3, 100* (msg.rtime-msg.otime)/(msg.rtt*1e3),
		-- 	(msg.ftime-msg.rtime)/1e3, 100*(msg.ftime - msg.rtime)/(msg.rtt*1e3))

		if msg.id < 1e6 then
			stat[msg.id] = msg
			self:send(msg.id+1)
		else
			chan:put(true)
		end
	end,
}

rawset(_G, 'ppq', ppq)

log.info("ppq created")

require 'fiber'.create(function()
	fiber.name("ppq.run")
	log.info("running ppq")
	ppq:run()
end)

require'fiber'.create(function() require'fiber'.sleep(1) log.info("ball") ppq:send(0) end)
chan:get()

local n = #stat
local floor = math.floor

table.sort(stat, function(a, b) return a.rtt < b.rtt end)
for _, p in ipairs{0.5, 0.9, 0.99, 0.999, 0.9999} do
	log.info("%.2f%% rtt: %.4fµs", p*100, stat[floor(n*p)].rtt)
end

log.info("----------------")

table.sort(stat, function(a, b) return a.syn < b.syn end)
for _, p in ipairs{0.5, 0.9, 0.99, 0.999, 0.9999} do
	log.info("%.2f%% syn: %.4fµs", p*100, stat[floor(n*p)].syn)
end

log.info("----------------")

table.sort(stat, function(a, b) return a.ack < b.ack end)
for _, p in ipairs{0.5, 0.9, 0.99, 0.999, 0.9999} do
	log.info("%.2f%% ack: %.4fµs", p*100, stat[floor(n*p)].ack)
end

os.exit(0)
