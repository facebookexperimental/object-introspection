#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <vector>
// #include "common/init/Init.h"

static char textPad __attribute__((used));

// pass in the loop counter in the args
std::vector<int> doStuff(std::vector<int> &f, int i) {
  std::vector<int> altvect = {1, 3, 5, 7};

  std::cout << " doStuff entries: " << f.size() << std::endl;
  std::cout << " addr of f = " << reinterpret_cast<void *>(&f) << std::endl;

  for (int j = 0; j < 40; ++j) {
    f.push_back(j);
  }

  std::cout << " doStuff entries: " << f.size() << std::endl;

#if 0
  if (i % 2)
  {
    std::vector<int> altvect = { 1,3,5,7 };
    return altvect;
    /* asm("" : : "a"(altvect)); */
    /* __asm__ volatile ("movl %%rax %0"
                    ::"m"(altvect):); */
    /* __asm__ volatile ("retq"); */
  }
#endif

  std::cout << syscall(SYS_gettid) << " " << i << "f = " << std::hex << &f
            << std::endl;

  std::vector<int> newvect(altvect);

  std::cout << " addr of newvect = " << reinterpret_cast<void *>(&newvect)
            << std::endl;

  return newvect;
}

#if 0
void doStuff(std::vector<int> &f, int i)
{
  f.push_back(i);

  if (i > 1000000)
    __asm__ volatile ("retq");

  std::cout << syscall(SYS_gettid)  << " " << i
            << "f = " << std::hex << &f <<  std::endl;
}
#endif

void *doit(void *arg) {
  int *loopcnt = reinterpret_cast<int *>(arg);
  std::vector<int> f;

  for (int i = 0; i < *loopcnt; ++i) {
    std::vector<int> g = doStuff(f, i);

    std::cout << "Number of elems = " << g.size() << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(10000));

    f.clear();
  }

  pthread_exit(arg);
}

int main(int argc, char *argv[]) {
  int i = 0;
  int err;
  pthread_t tid[2];
  char *b;

  // facebook::initFacebook(&argc, &argv);

  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <loopcnt> " << std::endl;
    exit(1);
  }

  int loopcnt = atoi(argv[1]);

  std::cout << "main thread = " << syscall(SYS_gettid) << " pid = " << getpid()
            << std::endl;
  sleep(1);

  std::map<std::string, int> mapOfWords;
  mapOfWords.insert(std::make_pair("earth", 1));
  mapOfWords.insert(std::make_pair("moon", 2));
  mapOfWords["sun"] = 3;

  std::vector<std::string> nameList;
  nameList.push_back("The quick brown fox");
  nameList.push_back("jumps over ");
  nameList.push_back("the ");
  nameList.push_back("lazy dog ");

  for (auto it = nameList.begin(); it != nameList.end(); it++) {
    std::cout << "nameList: " << *it << " size: " << it->size() << std::endl;
  }

  std::cout << "mapOfWords #elements: " << mapOfWords.size() << std::endl;

  int size = 0;

  for (auto it = mapOfWords.begin(); it != mapOfWords.end(); ++it) {
    size += it->first.size();
  }

  std::cout << "mapOfWords map addr = " << &mapOfWords << std::endl;
  std::cout << "nameList vector addr = " << &nameList << std::endl;

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
