#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

struct xtm_queue;

/*
 * Create new struct xtm_queue
 * @param[in] size - queue size, must be power of two
 * @return new xtm_queue or NULL in case of error, and sets the errno value
 */
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

/*
 * Helper function, invoked all queue messages
 * Return 0 if success, otherwise return -1 and set errno value
 */
static inline int
xtm_fun_invoke_all(struct xtm_queue *queue)
{
	int rc = xtm_fun_invoke_with_pipe_flushing(queue);
	while (rc > 0)
		rc = xtm_fun_invoke(queue);
	return rc;
}

/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

