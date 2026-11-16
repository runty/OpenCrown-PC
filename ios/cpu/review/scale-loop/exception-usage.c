// Private observational wrapper. The real implementation is included unchanged;
// this must not be linked into a shipping app.
#define __remill_fpu_exception_test dcp_real_exception_test
#define __remill_fpu_exception_clear dcp_real_exception_clear
#define __remill_fpu_exception_raise dcp_real_exception_raise
#include "../../fp80/exceptions.c"
#undef __remill_fpu_exception_test
#undef __remill_fpu_exception_clear
#undef __remill_fpu_exception_raise
#include <stdatomic.h>
#include <stdio.h>
static _Atomic unsigned long long tests,invalid_results,clears,raises;
int32_t __remill_fpu_exception_test(int32_t mask) {
  int32_t result=dcp_real_exception_test(mask);
  atomic_fetch_add_explicit(&tests,1,memory_order_relaxed);
  if(result&1) atomic_fetch_add_explicit(&invalid_results,1,memory_order_relaxed);
  return result;
}
void __remill_fpu_exception_clear(int32_t mask) {
  atomic_fetch_add_explicit(&clears,1,memory_order_relaxed);
  dcp_real_exception_clear(mask);
}
void __remill_fpu_exception_raise(int32_t mask) {
  atomic_fetch_add_explicit(&raises,1,memory_order_relaxed);
  dcp_real_exception_raise(mask);
}
__attribute__((destructor)) static void report_exception_usage(void) {
  fprintf(stderr,"EXCEPTION_USAGE tests=%llu invalid_results=%llu clears=%llu raises=%llu\n",
          atomic_load(&tests),atomic_load(&invalid_results),atomic_load(&clears),atomic_load(&raises));
}
