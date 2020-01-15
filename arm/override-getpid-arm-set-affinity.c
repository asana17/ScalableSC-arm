#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sched.h>

#define ASSERT(x) if (!(x)) {fprintf(stderr, "failed(%d)\n", __LINE__); _exit(1);}

static inline uint64_t vtimer() {
  uint64_t virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
}

typedef struct {
  void *data __attribute__ ((aligned(64)));
  uint64_t app_cnt;
  uint64_t os_cnt;
} Entry;

#ifndef RINGBUFFER_ENTRY_NUM
#define RINGBUFFER_ENTRY_NUM 256
#endif

#define OS_CORE_LIMIT 32

#define SLEEP_LIMIT 100000000

typedef struct {
  uint64_t app_cnt __attribute__ ((aligned(64)));
  uint64_t os_cnt __attribute__ ((aligned(64)));
  Entry entry[RINGBUFFER_ENTRY_NUM] __attribute__ ((aligned(64)));
} RingBuffer;
RingBuffer buf;



typedef struct {
  int flag;
  pid_t res;
} GetpidRequest;

typedef struct {
  int status;
  int type;

  union {
    GetpidRequest getpid;
  } arguments;
} Request;

int delegate_stop = 0;

#define REQUEST_TYPE_GETPID 0


pid_t getpid();
pid_t getpid(){
  Request data;
  GetpidRequest *getpid_rq= &data.arguments.getpid;
  data.status = 0;
  data.type= REQUEST_TYPE_GETPID;
  getpid_rq->flag = 1;

  uint64_t cnt = __sync_fetch_and_add(&buf.app_cnt, 1);
  Entry *const entry = &buf.entry[cnt % RINGBUFFER_ENTRY_NUM];
  asm volatile("sev":::"memory");

  while(entry->app_cnt != cnt) {
    asm volatile("wfe":::"memory");
  }

  //check if the entry is already allocated by another thread.
  // if we fail to set data, the entry is allocated and we have to retry.
  while(entry->data != NULL) {
    asm volatile("wfe":::"memory");
  }

  asm volatile("":::"memory");
  entry->data = (void*)&data;

  asm volatile("":::"memory");
  entry->app_cnt += RINGBUFFER_ENTRY_NUM;
  asm volatile("":::"memory");

  //wait until consumer notifies function return
  while(data.status == 0) {
    asm volatile("wfe":::"memory");
  }
  asm volatile("":::"memory");
  return getpid_rq->res;
}

void *delegate_func(void *arg);
void *delegate_func(void *arg) {
  puts("delegate_func");
  while(1) {
    uint64_t cnt = __sync_fetch_and_add(&buf.os_cnt, 1);
    Entry *const entry = &buf.entry[cnt % RINGBUFFER_ENTRY_NUM];
    asm volatile("sev":::"memory");

    while(entry->os_cnt != cnt) {
      asm volatile("wfe":::"memory");
    }
    
    uint64_t t1 = vtimer();

    Request *data;
    while((data = (void*)entry->data) == NULL) {
      uint64_t t2 = vtimer();
      if ((t2 - t1 > SLEEP_LIMIT) && (cnt == buf.app_cnt) && __sync_bool_compare_and_swap(&buf.app_cnt, cnt, cnt+1)) {
        asm volatile("":::"memory");
        ASSERT(entry->data == NULL);
        entry->app_cnt += RINGBUFFER_ENTRY_NUM;
        entry->os_cnt += RINGBUFFER_ENTRY_NUM;
        asm volatile("":::"memory");
        goto sleep;
      }
      
      if (delegate_stop) {
        ASSERT(0);
        return NULL;
      }
      asm volatile("wfe":::"memory");
    }
    
    asm volatile("":::"memory");
    entry->data = NULL;
    asm volatile("sev":::"memory");

    asm volatile("":::"memory");
    entry->os_cnt += RINGBUFFER_ENTRY_NUM;
    asm volatile("":::"memory");

   switch(data->type) {
    case REQUEST_TYPE_GETPID:
      do {
        GetpidRequest *getpid_rq= &data->arguments.getpid;
        pid_t (*origgetpid)()= dlsym(RTLD_NEXT, "getpid");
        getpid_rq->res = origgetpid();
      } while (0);
      break;
    }

    asm volatile("":::"memory");

    //notify function return to producer
    data->status = 1;
    asm volatile("sev":::"memory");

    continue;

  sleep:
    usleep(1000*10);
  }
  ASSERT(0);
  return NULL;
}

void delegate_init(void);
void delegate_init(void) {
  buf.app_cnt = 0;
  buf.os_cnt = 0;
  ASSERT(sizeof(Entry) == 64);
  for (int i = 0; i < RINGBUFFER_ENTRY_NUM; i++) {
    Entry *e = &buf.entry[i];
    e->data = NULL;
    e->app_cnt = i;
    e->os_cnt = i;
  }
}

#define THREAD_NUM 4
pthread_t threads[THREAD_NUM];

__attribute__((constructor))
static void constructor() {
  cpu_set_t cpu_set;
  delegate_init();
  for (int i = 0; i < THREAD_NUM; i++) {
    CPU_ZERO(&cpu_set);
    CPU_SET(19-i,&cpu_set);
    pthread_create(&threads[i], NULL, delegate_func, NULL);
    pthread_setaffinity_np(threads[i], sizeof(cpu_set_t), &cpu_set);
  }
  for (int i = 0; i < THREAD_NUM; i++) {
    pthread_detach(threads[i]);
  }
}
