#import "coordinated-read.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

@interface TestCoordinator : NSFileCoordinator
@property(nonatomic) BOOL invokesAccessor;
@property(nonatomic) BOOL returnsError;
@end
@implementation TestCoordinator
- (void)coordinateReadingItemAtURL:(NSURL *)url
                          options:(NSFileCoordinatorReadingOptions)options
                            error:(NSError **)error
                       byAccessor:(void (NS_NOESCAPE ^)(NSURL *))accessor {
  (void)options;
  if (self.invokesAccessor) accessor(url);
  if (self.returnsError && error)
    *error = [NSError errorWithDomain:@"provider" code:42 userInfo:nil];
}
@end
static void check(bool passed) { if (!passed) std::abort(); }
int main() {
  @autoreleasepool {
    NSURL *url = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
    TestCoordinator *coordinator = [[TestCoordinator alloc] init];
    for (bool invoked : {false, true}) for (bool error : {false, true}) {
      coordinator.invokesAccessor = invoked;
      coordinator.returnsError = error;
      __block unsigned calls = 0;
      NSError *result = DCPCoordinatedRead(coordinator, url, ^(NSURL *selected) {
        check([selected isEqual:url]); ++calls;
      });
      check(calls == (invoked ? 1u : 0u));
      if (error) check([result.domain isEqualToString:@"provider"] && result.code == 42);
      else if (!invoked) check([result.domain isEqualToString:@"OpenCrownFileCoordination"]);
      else check(result == nil);
    }
    NSFileCoordinator *real = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    __block bool read = false;
    NSError *result = DCPCoordinatedRead(real, url, ^(NSURL *selected) {
      read = selected != nil;
    });
    check(read && result == nil);
    std::puts("PASS: coordination success, omitted accessor, provider errors, and real local read");
  }
}
