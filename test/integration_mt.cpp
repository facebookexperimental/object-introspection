#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

struct Foo {
  std::vector<std::vector<std::vector<int>>> xs;
};

Foo& doStuff(Foo& f) {
  std::vector<std::vector<int>> xs = {{5, 6, 7, 8}};
  f.xs.emplace_back(std::move(xs));
  return f;
}

void run(long iteration_count) {
  for (long i = 0; i < iteration_count; ++i) {
    std::chrono::microseconds sleep_time{std::rand() & 0x7f};
    std::this_thread::sleep_for(sleep_time);

    Foo f{{
        {
            {1, 2, 3},
            {4, 5, 6},
        },
    }};

    doStuff(f);
  }
}

void usage(char* prog, int exit_status) {
  std::printf("usage: %s <iteration_count> <thread_count>\n", prog);
  exit(exit_status);
}

int main(int argc, char* argv[]) {
  if (argc != 3)
    usage(argv[0], EXIT_FAILURE);

  long iteration_count = atol(argv[1]);
  if (iteration_count <= 0)
    usage(argv[0], EXIT_FAILURE);

  int thread_count = atoi(argv[2]);
  if (thread_count <= 0)
    usage(argv[0], EXIT_FAILURE);

  std::srand(std::time(nullptr));

  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; ++i)
    threads.emplace_back(run, iteration_count);

  for (auto& t : threads)
    t.join();

  return EXIT_SUCCESS;
}
