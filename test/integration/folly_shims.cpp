#include <folly/lang/SafeAssert.h>

namespace folly {
namespace detail {

// The FOLLY_SAFE_DCHECK macro is peppered throughout the folly headers. When
// building in release mode this macro does nothing, but in debug builds it
// requires safe_assert_terminate() to be defined. To avoid building and
// linking against folly, we define our own no-op version of this function here.
template <>
void safe_assert_terminate<false>(safe_assert_arg const* /*arg*/,
                                  ...) noexcept {
  abort();
}

template <>
void safe_assert_terminate<true>(safe_assert_arg const* /*arg*/, ...) noexcept {
  abort();
}

}  // namespace detail
}  // namespace folly
