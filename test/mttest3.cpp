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

// typedef struct vec_struct VEC;

struct vec_struct {
  std::vector<int> v;
};

class BaseFoo {
  int base[64];
};

enum Color { Red, Green, Blue };

#define DIM 3

class Foo : BaseFoo {
 public:
  enum Color { red, green, blue } col;
  ST a[8][8];
  vec_struct b[DIM][DIM][DIM];
  Color color;
  char c[10];
  // VEC d[10];
  std::unique_ptr<ST> ptr;
  std::shared_ptr<ST> ptr2;
  std::vector<std::string> vectorOfStr;
  std::vector<std::map<std::string, std::string>> mapOfStr;
  std::vector<std::pair<std::string, double>> vectorOfPair;
  UN unionVar;
  UN2 unionVar2;
  ST structVar;
  ST2 structVar2;
  int ref;
};

// pass in the loop counter in the args
std::vector<int> doStuff(Foo &foo,
                         std::vector<std::map<std::string, std::string>> &m,
                         std::vector<std::string> &f,
                         std::vector<std::pair<std::string, double>> &p) {
  std::vector<int> altvect = {1, 3, 5, 7};
  foo.ref++;

  std::cout << " doStuff entries: " << f.size() << std::endl;
  std::cout << " addr of f = " << reinterpret_cast<void *>(&f) << std::endl;
  std::cout << " addr of m = " << reinterpret_cast<void *>(&m) << std::endl;
  std::cout << " addr of p = " << reinterpret_cast<void *>(&p) << std::endl;

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
  std::vector<std::string> f;
  std::vector<std::map<std::string, std::string>> mv;

  std::map<std::string, std::string> m;
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

  Foo foo;

  for (int i = 0; i < DIM; i++) {
    for (int j = 0; j < DIM; j++) {
      for (int k = 0; k < DIM; k++) {
        int elems = i * DIM * DIM + j * DIM + k;
        for (int m = 0; m < elems; m++) {
          foo.b[i][j][k].v.push_back(10);
        }
      }
    }
  }

  for (int i = 0; i < *loopcnt; ++i) {
    for (int j = 0; j < 3; j++) {
      f.push_back("abcdefghijklmn");
    }
    for (int j = 0; j < 3; j++) {
      f.push_back(
          "abcdefghijklmnoaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }

    foo.vectorOfStr = f;
    foo.mapOfStr = mv;
    foo.vectorOfPair = pv;

    std::vector<int> g = doStuff(foo, mv, f, pv);

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
