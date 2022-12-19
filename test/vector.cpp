#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <vector>

class Emp {
  std::string name;
  int age;
  std::vector<Emp> colleagues;

 public:
  Emp(std::string name1) {
    name = name1;
  };
  void setName(std::string name1) {
    name = name1;
    std::cout << "name: " << name << std::endl;
  };
  std::string getName(void) {
    return name;
  };
  void setAge(int age) {
    age = age;
  };
  void addColleague(Emp p) {
    colleagues.emplace(colleagues.begin(), p);
  };
  std::vector<Emp> getColleagues(void) {
    return colleagues;
  }
};

class Comp {
  std::string name;
  std::vector<Comp> companies;

 public:
  Comp(std::string name1) {
    name = name1;
  };
  std::vector<Comp> getComps(void) {
    return companies;
  }
};

void foo(class Emp& me) {
  std::vector<Emp> colleagues = me.getColleagues();
  int size = colleagues.size();
  std::cout << size << std::endl;
  std::cout << colleagues[4].getName().size() << std::endl;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <loopcnt> " << std::endl;
    exit(1);
  }

  int loopcnt = atoi(argv[1]);

  Emp Jon("Jon Haslam");
  Emp Mark(std::string("Mark Santaniello"));
  Emp Kalyan(std::string("Kalyan Saladi"));
  Emp Banit(std::string("Banit Agrawal"));
  Emp Harit(std::string("Harit Modi"));
  Emp Amlan(std::string("Amlan Nayak"));
  Emp Blaise(std::string("Blaise Sanouillet"));

  Jon.addColleague(Mark);
  Jon.addColleague(Kalyan);
  Jon.addColleague(Banit);
  Jon.addColleague(Harit);
  Jon.addColleague(Amlan);
  Jon.addColleague(Blaise);

  std::vector<int> TestVec{10, 20, 30, 20, 10, 40};

  int total = 0;
  for (auto it = TestVec.begin(); it != TestVec.end(); it++) {
    total += *it;
  }

  std::cout << "Total = "
            << " " << total << std::endl;

  Comp bus("facebook");
  std::vector<Comp> m = bus.getComps();
  std::cout << "Number of Comps: " << m.size() << std::endl;

  std::vector<Emp> mycol = Jon.getColleagues();

  for (auto it = mycol.begin(); it != mycol.end(); ++it) {
    std::cout << "Colleague: " << it->getName().size()
              << " size: " << sizeof(*it) << std::endl;
  }

  // std::cout << "mycol addr = " << &mycol << std::endl;
  std::cout << "TestVec addr = " << &TestVec << std::endl;
  // std::cout << "Number of Colleagues: " << mycol.size() << std::endl;

  std::cout << "pid == " << getpid() << " (hit RETURN to continue)"
            << std::endl;

  for (int i = 0; i < loopcnt; i++) {
    std::cout << "i: " << i << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  foo(Jon);
}
