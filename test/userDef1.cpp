#include <unistd.h>

#include <chrono>
#include <future>
#include <iostream>
#include <vector>

typedef struct userDef {
  int var1;
  char var2;
  void *var3;
  char var4[256];
} userDef;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <loopcnt> " << std::endl;
    exit(1);
  }

  userDef udefs[10];

  int loopcnt = atoi(argv[1]);

  std::vector<userDef> userDefList;

  for (int i = 0; i < 10; ++i) {
    userDefList.push_back(userDef());
  }

  int i = 0;
  for (auto udef : userDefList) {
    std::cout << "userDefList[" << i << "]= " << sizeof(udef) << std::endl;
  }

  std::cout << "pid = " << getpid()
            << " Address of userDefList = " << &userDefList << std::endl;
  ;

  for (int i = 0; i < loopcnt; i++) {
    std::cout << "i: " << i << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}
