#pragma once
#import <Foundation/Foundation.h>

// Coordination is synchronous. Some cancellation/provider failures can prevent
// the accessor from running; only an invoked accessor may report success.
static inline NSError *DCPCoordinatedRead(
    NSFileCoordinator *coordinator, NSURL *source,
    void (^accessor)(NSURL *)) {
  __block BOOL entered = NO;
  NSError *error = nil;
  [coordinator coordinateReadingItemAtURL:source
                                  options:NSFileCoordinatorReadingWithoutChanges
                                    error:&error
                               byAccessor:^(NSURL *url) {
                                 entered = YES;
                                 accessor(url);
                               }];
  if (error)
    return error;
  if (!entered)
    return [NSError errorWithDomain:@"OpenCrownFileCoordination"
                               code:NSFileReadUnknownError
                           userInfo:@{NSLocalizedDescriptionKey :
                                          @"The selected folder could not be read."}];
  return nil;
}
