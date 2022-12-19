#include <folly/Function.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <deque>
#include <future>
#include <iostream>
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>
// #include "common/init/Init.h"

static char textPad __attribute__((used));

struct __attribute__((__packed__)) Foo {
  char *p; /* 8 bytes */
  char c;  /* 1 byte  */
  long x;  /* 8 bytes */
};

// struct Foo {
//   char *p;  /* 8 bytes */
//   char c;   /* 8 byte  */
//   long x;   /* 8 bytes */
// };

Foo myGlobalFoo;

// pass in the loop counter in the args
int doStuff(Foo &foo) {
  return foo.x;
}

void *doit(void *arg) {
  int *loopcnt = reinterpret_cast<int *>(arg);

  for (int i = 0; i < *loopcnt; ++i) {
    Foo foo;
    foo.x = *loopcnt;

    int result = doStuff(foo);

    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
  }
  pthread_exit(arg);
}

int main(int argc, char *argv[]) {
  int i = 0;
  int err;
  pthread_t tid[2];
  char *b;

  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <loopcnt> " << std::endl;
    exit(1);
  }

  int loopcnt = atoi(argv[1]);

  std::cout << "main thread = " << syscall(SYS_gettid) << " pid = " << getpid()
            << std::endl;
  sleep(1);

  for (int i = 0; i < 1; ++i) {
    err = pthread_create(&(tid[i]), NULL, &doit, (void **)&loopcnt);

    if (err != 0) {
      std::cout << "Failed to create thread:[ " << strerror(err) << " ]"
                << std::endl;
    }
  }

  for (int i = 0; i < loopcnt; i++) {
    std::cout << "i: " << i << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  for (int i = 0; i < 1; ++i) {
    pthread_join(tid[i], (void **)&b);
  }

  exit(EXIT_SUCCESS);
}
