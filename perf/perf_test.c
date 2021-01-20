#include "../xtm_api.h"

#undef NDEBUG

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

#define XTM_THREAD_SIZE 16

static struct xtm_queue *producer_queue;
static struct xtm_queue *consumer_queue;
int is_running = 0;
unsigned long long msg_count = 0;

struct msg {
	unsigned long long counter;
};

static void
alarm_sig_handler(int signum)
{
	(void)signum;
	__atomic_store_n(&is_running, 0, __ATOMIC_RELEASE);
}

static void
producer_msg_func(void *arg)
{
	free(arg);
}

static void
consumer_msg_func(void *arg)
{
	assert (producer_queue != NULL);
	struct msg *msg = (struct msg *)arg;
	if (xtm_msg_probe(producer_queue) == 0) {
		assert(xtm_fun_dispatch(producer_queue, producer_msg_func, msg, 0) == 0);
	} else {
		free(msg);
	}
}

static void
enqueue_message(void)
{
	if (consumer_queue == NULL)
		return;

	struct msg *msg = (struct msg *) malloc(sizeof(struct msg));
	assert(msg != NULL);
	if (xtm_msg_probe(consumer_queue) == 0) {
		msg->counter = msg_count++;
		assert(xtm_fun_dispatch(consumer_queue, consumer_msg_func, msg, 0) == 0);
	}
	else {
		free(msg);
	}
}

static void *
consumer_thread_func(void *arg)
{
	(void)arg;
	assert((consumer_queue = xtm_create(XTM_THREAD_SIZE)) != NULL);
	int consumer_pipe_fd = xtm_fd(consumer_queue);

	while (__atomic_load_n(&is_running, __ATOMIC_ACQUIRE) == 1) {
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(consumer_pipe_fd, &readset);
		int rc = select(consumer_pipe_fd + 1, &readset, NULL, NULL, NULL);
		if (rc < 0 && errno == EINTR)
			break;
		assert(rc > 0);

		if (FD_ISSET(consumer_pipe_fd, &readset))
		    assert(xtm_fun_invoke_all(consumer_queue) == 0);
	}
	/*
	 * Flush queue
	 */
	assert(xtm_fun_invoke_all(consumer_queue) == 0);
	return (void *)NULL;
}

static void *
producer_thread_func(void *arg)
{
	(void)arg;
	assert((producer_queue = xtm_create(XTM_THREAD_SIZE)) != NULL);
	int producer_pipe_fd = xtm_fd(producer_queue);
	__atomic_store_n(&is_running, 1, __ATOMIC_RELEASE);

	while (__atomic_load_n(&is_running, __ATOMIC_ACQUIRE) == 1) {
		fd_set readset;
		FD_ZERO(&readset);
		FD_SET(producer_pipe_fd, &readset);
		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 1;
		int rc = select(producer_pipe_fd + 1, &readset, NULL, NULL, &timeout);
		if (rc < 0 && errno == EINTR) {
			enqueue_message();
			break;
		}
		assert(rc >= 0);
		if (rc == 0) {
			enqueue_message();
			continue;
		}
		if (FD_ISSET(producer_pipe_fd, &readset))
		    assert(xtm_fun_invoke_all(producer_queue) == 0);

		if (__atomic_load_n(&is_running, __ATOMIC_ACQUIRE) == 0)
			enqueue_message();
	}
	/*
	 * Flush queue
	 */
	assert(xtm_fun_invoke_all(producer_queue) == 0);
	return (void *)NULL;
}

int main()
{
	pthread_t thread_1, thread_2;
	assert(pthread_create(&thread_1, NULL, producer_thread_func, (void *)1) == 0);
	while (__atomic_load_n(&is_running, __ATOMIC_ACQUIRE) != 1)
		;
	assert(pthread_create(&thread_2, NULL, consumer_thread_func, (void *)2) == 0);
	alarm(5);
	signal(SIGALRM, alarm_sig_handler);
	pthread_join(thread_1, NULL);
	pthread_join(thread_2, NULL);
	fprintf(stderr, "Perf test msg count %llu\n", msg_count);
	return EXIT_SUCCESS;
}