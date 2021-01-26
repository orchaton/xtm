local ffi = require 'ffi'

local xtm = ffi.load("../libxtm_api.so")
ffi.cdef[[
struct xtm_queue *
xtm_create(const unsigned size);

/*
 * Destroy xtm_queue, and free all resources, associated with it
 * @return 0 if success, otherwise return -1 and sets the errno value
 */
int
xtm_delete(struct xtm_queue *queue);

/*
 * Notify queue consumer about new messages in queue
 * @return 0 if success, otherwise return -1 and sets the errno value
 */
int
xtm_msg_notify(struct xtm_queue *queue);

/*
 * Return 0 in case there is free space in queue,
 * otherwise return -1 and sets the errno value to ENOBUFS
 */
int
xtm_msg_probe(struct xtm_queue *queue);

/*
 * Puts a message containing the function and its argument in the queue
 * if delayed == 0 and message count was zero before we call this function,
 * notifies queue consumer.
 * @return 0 if success, otherwise return -1 and sets the errno value
 */
int
xtm_fun_dispatch(struct xtm_queue *queue, void (*fun)(void *),
		 void *fun_arg, int delayed);

/*
 * Return queue file descriptor
 */
int
xtm_fd(const struct xtm_queue *queue);

/*
 * Retrieves messages from the queue and calls functions contained in them
 * @return count of executed functions.
 */
int
xtm_fun_invoke(struct xtm_queue *queue);

/*
 * Retrieves messages from the queue and calls functions contained in them,
 * also flushing queue pipe
 * @return count of executed functions if success, otherwise return -1
 *         and set errno value
 */
int
xtm_fun_invoke_with_pipe_flushing(struct xtm_queue *queue);
]]

ffi.cdef[[
	struct module {
		thread_event_t ev;
		struct xtm_queue *tx_xtm;
		struct xtm_queue *xtm_tx;
		void (*callback)(void *);
	};

	struct message_t {
		uint64_t sync;
		double time;
	};
]]

return {
	xtm_fun_invoke_all = function(q)
		local rc = xtm.xtm_fun_invoke_with_pipe_flushing(q)
		while rc > 0 do
			rc = xtm.xtm_fun_invoke(q)
		end
		return rc
	end,

	xtm_create = xtm.xtm_create,
	xtm_delete = xtm.xtm_delete,
	xtm_msg_notify = xtm.xtm_msg_notify,
	xtm_msg_probe = xtm.xtm_msg_probe,
	xtm_fun_dispatch = xtm.xtm_fun_dispatch,
	xtm_fd = xtm.xtm_fd,
	xtm_fun_invoke = xtm.xtm_fun_invoke,
	xtm_fun_invoke_with_pipe_flushing = xtm.xtm_fun_invoke_with_pipe_flushing,
}