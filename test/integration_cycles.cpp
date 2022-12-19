#include <stdio.h>
#include <unistd.h>

#include <cstring>
#include <memory>

struct RawNode {
  uint64_t value;
  struct RawNode* next;
};

void __attribute__((noinline)) rawPointerCycle(struct RawNode* node) {
  for (int i = 0; node && i < 10; i++) {
    node->value++;
    node = node->next;
  }
}

struct UniqueNode {
  uint64_t value;
  std::unique_ptr<struct UniqueNode> next;
};

void __attribute__((noinline)) uniquePointerCycle(struct UniqueNode* node) {
  for (int i = 0; node && i < 10; i++) {
    node->value++;
    node = node->next.get();
  }
}

struct SharedNode {
  uint64_t value;
  std::shared_ptr<struct SharedNode> next;
};

void __attribute__((noinline)) sharedPointerCycle(struct SharedNode* node) {
  for (int i = 0; node && i < 10; i++) {
    node->value++;
    node = node->next.get();
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "One of 'raw', 'unique', or 'shared' should be provided as an "
            "argument\n");
    return EXIT_FAILURE;
  }
  if (strcmp(argv[1], "raw") == 0) {
    RawNode third{2, nullptr}, second{1, &third}, first{0, &second};
    third.next = &first;
    for (int i = 0; i < 1000; i++) {
      rawPointerCycle(&first);
      sleep(1);
    }
  } else if (strcmp(argv[1], "unique") == 0) {
    std::unique_ptr<UniqueNode> first = std::make_unique<UniqueNode>();
    UniqueNode* firstPtr = first.get();
    first->next = std::make_unique<UniqueNode>();
    first->next->next = std::make_unique<UniqueNode>();
    first->next->next->next = std::move(first);
    for (int i = 0; i < 1000; i++) {
      uniquePointerCycle(firstPtr);
      sleep(1);
    }
  } else if (strcmp(argv[1], "shared") == 0) {
    std::shared_ptr<SharedNode> first = std::make_shared<SharedNode>();
    SharedNode* firstPtr = first.get();
    first->next = std::make_shared<SharedNode>();
    first->next->next = std::make_shared<SharedNode>();
    first->next->next->next = first;
    for (int i = 0; i < 1000; i++) {
      sharedPointerCycle(firstPtr);
      sleep(1);
    }
  } else {
    fprintf(stderr,
            "One of 'raw', 'unique', or 'shared' should be provided as an "
            "argument\n");
    return EXIT_FAILURE;
  }
}
