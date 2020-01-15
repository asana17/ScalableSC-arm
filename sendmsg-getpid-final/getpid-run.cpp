#define _GNU_SOURCE
#include <chrono>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#define THREAD_NUM 24

void abrt_handler(int sig, siginfo_t *info, void *ctx);
volatile sig_atomic_t eflag = 0;

void *getpid(void *arg) {
  while(1) {
    getpid();
  }
  return NULL;
}

pthread_t thread[THREAD_NUM];

int main() {
  struct sigaction sa_sigabrt;
  memset(&sa_sigabrt, 0, sizeof(sa_sigabrt));
  sa_sigabrt.sa_sigaction = abrt_handler;
  sa_sigabrt.sa_flags = SA_SIGINFO;

  cpu_set_t cpu_set;

  /*for (int i = 0; i < 100000; i++) {
    getpid();
  }*/
  for (int i = 0; i < THREAD_NUM; i++) {
    CPU_ZERO(&cpu_set);
    CPU_SET(i, &cpu_set);
    pthread_create(&thread[i], NULL, getpid, NULL);
    pthread_setaffinity_np(thread[i], sizeof(cpu_set_t), &cpu_set);
  }

  if (sigaction(SIGINT, &sa_sigabrt, NULL) < 0) {
    exit(1);
  }
  while (!eflag) {}
  std::cout << "abrt success" << std::endl;
}

void abrt_handler(int sig, siginfo_t *info, void *ctx) {
  for (int i = 0; i < THREAD_NUM; i++) {
    pthread_cancel(thread[i]);
  }
  eflag = 1;
}
