#include <string>

struct custom_cmp {
  bool operator()(int a, int b) const {
    return a < b;
  }
};

struct OIDTestingTwoString {
  std::string first;
  std::string second;
};

struct customTwoStringEq {
  bool operator()(const OIDTestingTwoString& a, const OIDTestingTwoString& b) {
    return (a.first == a.first && a.second == b.second);
  }
};

struct customHash {
  std::size_t operator()(const OIDTestingTwoString& two) const {
    return ((std::hash<std::string>()(two.first) ^
             (std::hash<std::string>()(two.second))));
  }
};
