#include <folly/ScopeGuard.h>
#include <folly/container/detail/F14Table.h>
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

void ScopeGuardImplBase::terminate() noexcept {
  abort();
}

}  // namespace detail

namespace f14 {
namespace detail {

void F14LinkCheck<getF14IntrinsicsMode()>::check() noexcept {
}

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
EmptyTagVectorType kEmptyTagVector = {};
#endif

bool tlsPendingSafeInserts(std::ptrdiff_t /*delta*/) {
  /* Disable extra debugging re-hash by classifying all inserts as safe (return
   * true) */
  return true;
}

std::size_t tlsMinstdRand(std::size_t /*n*/) {
  /* Disable insert order perturbation by always returning 0 */
  return 0;
}

}  // namespace detail
}  // namespace f14

}  // namespace folly
