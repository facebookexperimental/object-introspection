#include <unistd.h>

#include <cstdlib>
#include <vector>

#define INLINE static inline __attribute__((always_inline))

template <class T>
INLINE std::vector<T> combine(const std::vector<T>& x,
                              const std::vector<T>& y) {
  std::vector<T> combined;
  combined.reserve(x.size() + y.size());
  for (auto& elem : x)
    combined.push_back(elem);
  for (auto& elem : y)
    combined.push_back(elem);
  return combined;
}

template <class T>
INLINE std::vector<T> flatten(const std::vector<std::vector<T>>& vec) {
  std::vector<T> flattened;
  for (auto& elem : vec)
    flattened = combine(elem, flattened);
  return flattened;
}

template <class T>
INLINE std::vector<T> flatten_combine(const std::vector<std::vector<T>>& x,
                                      const std::vector<std::vector<T>>& y) {
  auto x_flat = flatten(x);
  auto y_flat = flatten(y);
  return combine(x_flat, y_flat);
}

#define MAX_SIZE 256

void fill(std::vector<int>& vec, int n) {
  n %= MAX_SIZE;
  vec.clear();
  vec.reserve(n);
  for (int i = 0; i < n; i++)
    vec.push_back(rand());
}

void fill_vec(std::vector<std::vector<int>>& vec, int n) {
  n %= MAX_SIZE;
  vec.clear();
  vec.reserve(n);
  for (int i = 0; i < n; i++) {
    vec.emplace_back();
    auto& last = vec.back();
    fill(last, rand());
  }
}

int main() {
  size_t exit_code = 0;
  for (int i = 0; i < 100; i++) {
    std::vector<std::vector<int>> x;
    std::vector<std::vector<int>> y;
    fill_vec(x, rand());
    fill_vec(y, rand());
    auto result = flatten_combine(x, y);
    for (auto value : result)
      exit_code += rand() % (value + 1);
    sleep(1);
  }
  return (int)exit_code;
}
