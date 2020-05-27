/*
 * lessons learned here:
 * set inherit schedule to explicit so we use the provided settings
 * set FIFO before setting priority or it will not take
 * sigrtmin and max are 34 and 64 on ubuntu
 * a detached thread should not be joined
 * rt priorities in ubuntu = 1-99
 * clock_nanosleep returns errno- check result for EINTR, and don't check errno
 */
#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"
#include "stddef.h"
#include "string.h"

#include "sys/types.h"
#include "unistd.h"
#include "signal.h"
#include "time.h"
#include "errno.h"

#define GNU_SOURCE
#include "sched.h"

#include "pthread.h"

#include "commander.h"


#define BUF_SIZE (1024 * 1024)


volatile int gv_running = true;
volatile int gv_timer_counter = 0;
volatile int gv_copier_count = 0;
volatile int gv_copy_wait_count = 0;


static void set_affinity(command_t *self) {
  printf("Setting processor affinity\n");

  int processor = 0;
  if (self->arg != NULL)
  {
    sscanf(self->arg, "%d", &processor);
  }
  printf("processor = %d\n", processor);

  //cpu_set_t cpuset;

  //pid_t pid = getpid();

  //sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset);
}

void *copy_and_sleep(void *argument) {
  uint8_t *buffer1 = malloc(BUF_SIZE);
  uint8_t *buffer2 = malloc(BUF_SIZE);

  printf("Starting copy_and_sleep thread\n");
  while (gv_running) {
    memcpy(buffer1, buffer2, BUF_SIZE);
    memcpy(buffer2, buffer1, BUF_SIZE);

    usleep(1000);

    gv_copy_wait_count++;
  }

  return NULL;
}

void *copier(void *argument) {
  uint8_t *buffer1 = malloc(BUF_SIZE);
  uint8_t *buffer2 = malloc(BUF_SIZE);

  pthread_attr_t attr;
  pthread_getattr_np(pthread_self(), &attr);

  int policy = 0;
  pthread_attr_getschedpolicy(&attr, &policy);
  printf("current policy = %d (FIFO = %d, RR = %d)\n", policy, SCHED_FIFO, SCHED_RR);

  struct sched_param param;
  pthread_attr_getschedparam(&attr, &param);
  printf("priority is actually %d\n", param.sched_priority);

  pthread_attr_destroy(&attr);

  printf("Starting copier thread\n");
  while (gv_running) {
    memcpy(buffer1, buffer2, BUF_SIZE);
    memcpy(buffer2, buffer1, BUF_SIZE);

    gv_copier_count++;
  }

  return NULL;
}

pthread_t create_thread(void *(*func)(void*), int prio) {
  int result = 0;

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  result = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  if (result != 0) {
    printf("error from pthread_attr_setschedpolicy\n");
  }

  struct sched_param sched_param;
  result = pthread_attr_getschedparam(&attr, &sched_param);
  if (result != 0) {
    printf("error from pthread_attr_getschedparam\n");
  }

  sched_param.sched_priority = prio;
  result = pthread_attr_setschedparam(&attr, &sched_param);
  if (result != 0) {
    printf("error from pthread_attr_setschedparam (%d errno %d)\n", result, errno);
  }

  result = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  if (result != 0) {
    printf("error from pthread_attr_setinheritsched\n");
  }

  result = pthread_create(&thread, &attr, func, NULL);
  if (result != 0) {
    printf("error from pthread_create\n");
  }

#if 0
  result = pthread_detach(thread);
  if (result != 0) {
    printf("error from pthread_detach\n");
  }
#endif

  result = pthread_attr_destroy(&attr);
  if (result != 0) {
    printf("error from pthread_attr_destroy\n");
  }

  return thread;
}

void timer_handler(int signal, siginfo_t *info, void *arg) {
  gv_timer_counter++;
}

int main(int argc, char *argv[]) {
  int result = 0;

  printf("SIGRTMIN = %d\n", SIGRTMIN);
  printf("SIGRTMAX = %d\n", SIGRTMAX);

  printf("max rt prio = %d\n", sched_get_priority_max(SCHED_FIFO));
  printf("min rt prio = %d\n", sched_get_priority_min(SCHED_FIFO));

  printf("Starting signal test\n");
  command_t cmd;
  command_init(&cmd, argv[0], "0.0.1");

  command_option(&cmd, "-a", "--affinity [processor]", "Set processor affinity to a specific processor, or the first processor if none is given", set_affinity);
  command_parse(&cmd, argc, argv);

  struct sigaction actions;
  actions.sa_sigaction = timer_handler;
  actions.sa_flags = SA_RESTART;
  sigaction(SIGRTMIN, &actions, NULL);

  timer_t timer;
  struct sigevent events;
  events.sigev_notify = SIGEV_SIGNAL;
  events.sigev_signo = SIGRTMIN;

  result = timer_create(CLOCK_MONOTONIC, &events, &timer);
  if (result != 0) {
    printf("error from timer_create\n");
  }

  struct timespec timeout;
  timeout.tv_sec = 0;
  timeout.tv_nsec = 1000000;

  struct itimerspec timer_timeout;
  timer_timeout.it_value = timeout;
  timer_timeout.it_interval = timeout;
  result = timer_settime(timer, 0, &timer_timeout, NULL);

  pthread_t self_pid = pthread_self();
  printf("self pid = %lld\n", self_pid);
  struct sched_param self_sched;
  int policy = 0;
  result = pthread_getschedparam(pthread_self(), &policy, &self_sched);
  if (result != 0) {
    printf("error from pthread_getschedparam\n");
  }
  printf("policy for main was = %d\n", policy);
  printf("priority for main was = %d\n", self_sched.sched_priority);
  self_sched.sched_priority = 99;
  result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &self_sched);
  if (result != 0) {
    printf("error from pthread_setschedparam\n");
  }
  result = pthread_getschedparam(pthread_self(), &policy, &self_sched);
  if (result != 0) {
    printf("error from pthread_getschedparam\n");
  }
  printf("policy for main was = %d\n", policy);
  printf("priority for main was = %d\n", self_sched.sched_priority);

  printf("PThreads\n");
  pthread_t thread1 = create_thread(copy_and_sleep, 36);
  pthread_t thread2 = create_thread(copier, 99);

  uint64_t count = 0;
  for (uint64_t i = 0; i < 10000000000; i++)
  {
    count += i * count;
  }

  struct timespec wait_time;
  clock_gettime(CLOCK_MONOTONIC, &wait_time);
  wait_time.tv_sec += 1;
  wait_time.tv_nsec += 5000000;
  result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wait_time, &wait_time);
  while (result == EINTR) {
    result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wait_time, &wait_time);
  }
  if (errno != 0 && errno != EINTR) {
    printf("erro = %d\n", errno);
  }

  printf("Exiting\n");
  gv_running = false;

  result = pthread_join(thread1, NULL);
  if (result != 0) {
    printf("error from pthread_join %d %d\n", result, errno);
  }

  result = pthread_join(thread2, NULL);
  if (result != 0) {
    printf("error from pthread_join %d %d\n", result, errno);
  }

  printf("timer counter = %d\n", gv_timer_counter);
  printf("copier counter = %d\n", gv_copier_count);
  printf("copy wait counter = %d\n", gv_copy_wait_count);

  printf("Clean exit\n");
}