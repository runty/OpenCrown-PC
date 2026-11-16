#import "coordinated-read.h"
#import "save-transfer-ui.h"
#include "save-transactions.h"
#include "storage-core.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdexcept>

namespace {
NSError *transferError(NSString *text, NSInteger code = 1) {
  return [NSError errorWithDomain:@"OpenCrownSaveTransfer"
                             code:code
                         userInfo:@{NSLocalizedDescriptionKey : text}];
}
} // namespace
@interface DCPSaveTransferController ()
@property(nonatomic, strong) NSURL *documentsURL;
@property(nonatomic, weak) UIViewController *presenter;
@property(nonatomic, strong, nullable) NSURL *exportURL;
// A picker-granted location is only a hint for the next explicit selection.
// It is not a persistent security bookmark or an automatic sync destination.
@property(nonatomic, strong, nullable) NSURL *lastSaveFolder;
@property(nonatomic, strong, nullable) NSFileCoordinator *coordinator;
@property(nonatomic, strong, nullable) UIDocumentPickerViewController *picker;
@property(nonatomic, readwrite, getter=isBusy) BOOL busy;
@property(nonatomic) BOOL exporting;
@property(nonatomic) BOOL working;
@property(nonatomic) BOOL cancelled;
@end
@implementation DCPSaveTransferController
- (instancetype)initWithDocumentsURL:(NSURL *)url {
  if ((self = [super init]))
    self.documentsURL = url;
  return self;
}
- (void)finish:(NSString *)message error:(NSError *)error {
  NSAssert(NSThread.isMainThread, @"Save transfer UI owns main");
  if (self.exportURL)
    [NSFileManager.defaultManager removeItemAtURL:self.exportURL error:nil];
  self.exportURL = nil;
  self.coordinator = nil;
  self.picker = nil;
  self.busy = self.working = NO;
  if (self.completionHandler)
    self.completionHandler(message, error);
}
- (void)importFromFiles:(UIViewController *)presenter {
  NSAssert(NSThread.isMainThread, @"Save transfer UI owns main");
  if (self.busy)
    return;
  self.busy = YES;
  self.exporting = self.cancelled = NO;
  self.presenter = presenter;
  if (!presenter.view.window) {
    [self finish:nil
           error:transferError(@"Save import cancelled", NSUserCancelledError)];
    return;
  }
  UIDocumentPickerViewController *picker =
      [[UIDocumentPickerViewController alloc]
          initForOpeningContentTypes:@[ UTTypeFolder ]
                              asCopy:NO];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.directoryURL = self.lastSaveFolder;
  self.picker = picker;
  [presenter presentViewController:picker animated:YES completion:nil];
}
- (void)exportToFiles:(UIViewController *)presenter {
  NSAssert(NSThread.isMainThread, @"Save transfer UI owns main");
  if (self.busy)
    return;
  self.busy = self.working = self.exporting = YES;
  self.cancelled = NO;
  self.presenter = presenter;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    NSString *failure = nil;
    NSURL *result = nil;
    try {
      const auto root =
          std::filesystem::path(self.documentsURL.fileSystemRepresentation);
      dcp::storage::SaveTransactions saves(root / "ProbeSaves",
                                           root / "SaveCheckpoints");
      if (!saves.recover())
        throw std::runtime_error(saves.error());
      const auto transfers = root / "SaveTransfers";
      const auto id =
          dcp::storage::stage_save_transfer(root / "ProbeSaves", transfers);
      result = [NSURL fileURLWithPath:@((transfers / id).c_str())
                          isDirectory:YES];
    } catch (const std::exception &error) {
      failure = @(error.what());
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      self.working = NO;
      self.exportURL = result;
      if (failure || self.cancelled || !self.presenter.view.window) {
        [self finish:nil
               error:transferError(failure ?: @"Save export cancelled",
                                   failure ? 1 : NSUserCancelledError)];
        return;
      }
      UIDocumentPickerViewController *picker =
          [[UIDocumentPickerViewController alloc]
              initForExportingURLs:@[ result ]
                            asCopy:YES];
      picker.delegate = self;
      self.picker = picker;
      [self.presenter presentViewController:picker animated:YES completion:nil];
    });
  });
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)picker {
  if (picker != self.picker || !self.busy || self.working)
    return;
  [self finish:nil
         error:transferError(@"Save transfer cancelled", NSUserCancelledError)];
}
- (void)documentPicker:(UIDocumentPickerViewController *)picker
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  if (picker != self.picker || !self.busy || self.working)
    return;
  if (urls.count != 1) {
    [self finish:nil error:transferError(@"Choose one folder for save transfer.")];
    return;
  }
  if (self.exporting) {
    self.lastSaveFolder = urls.firstObject;
    [self finish:@"Save copy exported. Wait for Files to finish uploading "
                 @"before using it elsewhere."
           error:nil];
    return;
  }
  self.working = YES;
  NSURL *source = urls.firstObject;
  const BOOL scoped = [source startAccessingSecurityScopedResource];
  self.coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
  NSFileCoordinator *coordinator = self.coordinator;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    __block NSError *failure = nil;
    NSError *coordinationError = nil;
    coordinationError =
        DCPCoordinatedRead(coordinator, source, ^(NSURL *readURL) {
          try {
            const auto root = std::filesystem::path(
                self.documentsURL.fileSystemRepresentation);
            dcp::storage::SaveTransactions saves(root / "ProbeSaves",
                                                 root / "SaveCheckpoints");
            if (!saves.recover())
              throw std::runtime_error(saves.error());
            dcp::storage::import_save_folder(readURL.fileSystemRepresentation,
                                             root / "ProbeSaves",
                                             root / "SaveCheckpoints");
          } catch (const std::exception &error) {
            failure = transferError(@(error.what()));
          }
        });
    if (scoped)
      [source stopAccessingSecurityScopedResource];
    if (!failure)
      failure = coordinationError;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!failure)
        self.lastSaveFolder = source;
      [self finish:failure ? nil
                           : @"Save imported. Previous local progress is "
                             @"retained in Save Checkpoints."
             error:failure];
    });
  });
}
- (void)cancel {
  NSAssert(NSThread.isMainThread, @"Save transfer UI owns main");
  self.cancelled = YES;
  [self.coordinator cancel];
  // A running local transaction must finish before the launcher can play.
  if (self.busy && !self.working)
    [self
        finish:nil
         error:transferError(@"Save transfer cancelled", NSUserCancelledError)];
}
@end
