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
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#include "mttest.h"
// #include "common/init/Init.h"

static char textPad __attribute__((used));

typedef union {
  int i;
  float f;
  char c;
} UN;
typedef UN UN2;
typedef struct {
  int i;
  float f;
  char c;
} ST;
typedef ST ST2;

class FooParent {
  int aa;
};

class Foo : FooParent {
 public:
  bool aBool;
  short aShort;
  std::vector<std::string, std::allocator<std::string>> vectorOfStr;
  std::multimap<int, int> intToStrMultiMap;
  std::map<int, int, custom_cmp> map;
  std::unordered_map<OIDTestingTwoString, int, customHash, customTwoStringEq>
      unorderedMap;
  char arr[10];
  int aa;
  int bb : 1;
  int : 0;
  int cc : 5;
  int dd : 30;

  // Bar bar_arr[5];
  int ref;
  std::function<void(int)> testFunc;
  std::deque<int> testDeque;

  std::queue<int, std::vector<int>> testQueue;
};

Foo myGlobalFoo;
// std::unique_ptr<Foo> myGlobalFoo;

// pass in the loop counter in the args
#ifdef MTTEST2_INLINE_DO_STUFF
static inline __attribute__((always_inline))
#endif
std::vector<int>
doStuff(Foo &foo, std::vector<std::map<std::string, std::string>> &m,
        std::vector<std::string> &f,
        std::vector<std::pair<std::string, double>> &p) {
  std::vector<int> altvect = {1, 3, 5, 7};
  foo.ref++;
  myGlobalFoo.vectorOfStr.push_back(std::string("Test String"));

  std::cout << " doStuff entries: " << f.size() << std::endl;
  std::cout << " addr of f = " << reinterpret_cast<void *>(&f) << std::endl;
  std::cout << " addr of m = " << reinterpret_cast<void *>(&m) << std::endl;
  std::cout << " addr of p = " << reinterpret_cast<void *>(&p) << std::endl;
  std::cout << " addr of myGlobalFoo = "
            << reinterpret_cast<void *>(&myGlobalFoo) << std::endl;

  std::vector<int> newvect(altvect);

  std::cout << " addr of newvect = " << reinterpret_cast<void *>(&newvect)
            << std::endl;

  return newvect;
}

void doStuff(std::vector<int> &f, int i) {
  f.push_back(i);
  std::cout << "Entries in f: " << f.size() << std::endl;
}

void doNothing() {
  std::cout << "I do nothing, the function does nothing" << std::endl;
}

void *doit(void *arg) {
  doNothing();
  int *loopcnt = reinterpret_cast<int *>(arg);
  std::vector<std::string> f;
  f.reserve(200);
  std::vector<std::unordered_map<std::string, std::string>> mv;

  std::unordered_map<std::string, std::string> m;
  m["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"] = "ba";
  m["a"] = "baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  m["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"] =
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  m["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbbaaaaaaaaaaa"] = "bbb";
  m["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaacccccccccccaaaaaaaa"] = "bbbb";

  mv.push_back(m);
  mv.push_back(m);
  mv.push_back(m);

  std::vector<std::pair<std::string, double>> pv;
  {
    std::pair<std::string, double> p(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 10);
    pv.push_back(p);
  }
  {
    std::pair<std::string, double> p(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabcdef", 10);
    pv.push_back(p);
  }
  {
    std::pair<std::string, double> p(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabcdefghi", 10);
    pv.push_back(p);
  }

  for (int i = 0; i < *loopcnt; ++i) {
    for (int j = 0; j < 3; j++) {
      f.push_back("abcdefghijklmn");
    }
    for (int j = 0; j < 3; j++) {
      f.push_back(
          "abcdefghijklmnoaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    Foo foo;

    std::multimap<int, int> mm;

    mm.insert(std::pair<int, int>(0, 0));
    mm.insert(std::pair<int, int>(1, 10));
    mm.insert(std::pair<int, int>(2, 20));
    mm.insert(std::pair<int, int>(3, 30));

    mm.insert(std::pair<int, int>(1, 100));

    foo.intToStrMultiMap = mm;

    /*foo.vectorOfStr = f;
    foo.mapOfStr = mv;
    foo.vectorOfPair = pv;*/
    /*foo.bar_arr[0].bar = "";
    foo.bar_arr[1].bar = "0123456789";
    foo.bar_arr[2].bar = "01234567890123456789";
    foo.bar_arr[3].bar = "0123456789012345678901234567890123456789";
    foo.bar_arr[4].bar =
    "01234567890123456789012345678901234567890123456789012345678901234567890123456789";*/

    foo.testFunc = [](int n) { std::cout << n << std::endl; };
    foo.testQueue.push(1);
    foo.testQueue.push(2);
    foo.testQueue.push(3);

    foo.testDeque.push_back(5);

    std::vector<std::map<std::string, std::string>> dummy;
    std::vector<int> g = doStuff(foo, dummy, f, pv);
    doStuff(g, i);

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
