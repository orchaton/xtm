local log = require 'log'

local ppq = require 'ppq'.new {
	on_ctx = function(_, msg)
		log.info(msg)
	end,
}

require 'fiber'.create(function()
	ppq:run()
end)


require 'console'.start()
os.exit(0)