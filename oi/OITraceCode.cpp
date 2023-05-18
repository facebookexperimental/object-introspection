#define NDEBUG 1
// Required for compatibility with new glibc headers
#define __malloc__(x, y) __malloc__
#if !__has_builtin(__builtin_free)
#define __builtin_free(x) free(x)
#endif
#pragma clang diagnostic ignored "-Wunknown-attributes"

// clang-format off
// The header xmmintrin.h must come first. Otherwise it results in errors
// jemalloc during JIT compilation
#include <xmmintrin.h>
#include <cstdint>

#include <utility>
#include <unistd.h>

// clang-format on

#define C10_USING_CUSTOM_GENERATED_MACROS

// These globals are set by oid, see end of OIDebugger::compileCode()
extern uintptr_t dataBase;
extern size_t dataSize;
extern uintptr_t cookieValue;
extern int logFile;

constexpr int oidMagicId = 0x01DE8;

#include <array>

namespace {

class {
 private:
  // 1 MiB of pointers
  std::array<uintptr_t, (1 << 20) / sizeof(uintptr_t)> data;

  // twang_mix64 hash function, taken from Folly where it is used
  // as the default hash function for 64-bit integers
  constexpr static uint64_t twang_mix64(uint64_t key) noexcept {
    key = (~key) + (key << 21);  // key *= (1 << 21) - 1; key -= 1;
    key = key ^ (key >> 24);
    key = key + (key << 3) + (key << 8);  // key *= 1 + (1 << 3) + (1 << 8)
    key = key ^ (key >> 14);
    key = key + (key << 2) + (key << 4);  // key *= 1 + (1 << 2) + (1 << 4)
    key = key ^ (key >> 28);
    key = key + (key << 31);  // key *= 1 + (1 << 31)
    return key;
  }

 public:
  void initialize() noexcept {
    data.fill(0);
  }

  // Adds the pointer to the set.
  // Returns `true` if the value was newly added,
  // or `false` if the value was already present.
  bool add(uintptr_t pointer) noexcept {
    __builtin_assume(pointer > 0);
    uint64_t index = twang_mix64(pointer) % data.size();
    while (true) {
      uintptr_t entry = data[index];
      if (entry == 0) {
        data[index] = pointer;
        return true;
      }
      if (entry == pointer) {
        return false;
      }
      index = (index + 1) % data.size();
    }
  }
} static pointers;

void __jlogptr(uintptr_t ptr) {
  static constexpr char hexdigits[] = "0123456789abcdef";
  static constexpr size_t ptrlen = 2 * sizeof(ptr);

  static char hexstr[ptrlen + 1] = {};

  size_t i = ptrlen;
  while (i--) {
    hexstr[i] = hexdigits[ptr & 0xf];
    ptr = ptr >> 4;
  }
  hexstr[ptrlen] = '\n';
  write(logFile, hexstr, sizeof(hexstr));
}

}  // namespace

// Unforunately, this is a hack for AdFilterData.
class PredictorInterface;
class PredictionCompositionNode;

constexpr size_t kGEMMLOWPCacheLineSize = 64;

template <typename T>
struct AllocAligned {
  // Allocate a T aligned at an `align` byte address
  template <typename... Args>
  static T* alloc(Args&&... args) {
    void* p = nullptr;

#if defined(__ANDROID__)
    p = memalign(kGEMMLOWPCacheLineSize, sizeof(T));
#elif defined(_MSC_VER)
    p = _aligned_malloc(sizeof(T), kGEMMLOWPCacheLineSize);
#else
    posix_memalign((void**)&p, kGEMMLOWPCacheLineSize, sizeof(T));
#endif

    if (p) {
      return new (p) T(std::forward<Args>(args)...);
    }

    return nullptr;
  }

  // Free a T previously allocated via AllocAligned<T>::alloc()
  static void release(T* p) {
    if (p) {
      p->~T();
#if defined(_MSC_VER)
      _aligned_free((void*)p);
#else
      free((void*)p);
#endif
    }
  }
};

// Deleter object for unique_ptr for an aligned object
template <typename T>
struct AlignedDeleter {
  void operator()(T* p) const {
    AllocAligned<T>::release(p);
  }
};

// alignas(0) is ignored according to docs so can be default
template <unsigned int N, unsigned int align = 0>
struct alignas(align) DummySizedOperator {
  char c[N];
};

// The empty class specialization is, unfortunately, necessary. When this
// operator is passed as a template parameter to something like unordered_map,
// even though an empty class and a class with a single character have size one,
// there is some empty class optimization that changes the static size of the
// container if an empty class is passed.

// DummySizedOperator<0,0> also collapses to this
template <>
struct DummySizedOperator<0> {};
