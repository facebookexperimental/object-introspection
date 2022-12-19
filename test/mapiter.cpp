#include <unistd.h>

#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <vector>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <loopcnt> " << std::endl;
    exit(1);
  }

  int loopcnt = atoi(argv[1]);

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

  // std::map<std::string, int>::iterator it = mapOfWords.begin();
  // std::map<std::string, int>::iterator end = mapOfWords.end();

  int size = 0;

  for (auto it = mapOfWords.begin(); it != mapOfWords.end(); ++it) {
    size += it->first.size();
  }

  std::cout << "mapOfWords map addr = " << &mapOfWords << std::endl;
  std::cout << "nameList vector addr = " << &nameList << std::endl;

  std::cout << "pid == " << getpid() << " (hit RETURN to continue)"
            << std::endl;

  for (int i = 0; i < loopcnt; i++) {
    std::cout << "i: " << i << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  std::cout << "Total size of strings in mapOfWords is " << size << " bytes"
            << std::endl;

  return 0;
}
