#import "coordinated-read.h"
#import "storage-ui.h"
#include "storage-core.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <atomic>
#include <memory>

namespace {
NSError *storage_error(const std::string &message, NSInteger code = 1) {
  return [NSError errorWithDomain:@"OpenCrownStorage"
                             code:code
                         userInfo:@{
                           NSLocalizedDescriptionKey : @(message.c_str())
                         }];
}
} // namespace
@interface DCPImportProgressController : UIViewController
@property(nonatomic, strong) UILabel *label;
@property(nonatomic, strong) UIProgressView *progress;
@property(nonatomic, strong) UIButton *cancelButton;
@property(nonatomic, copy) void (^cancelHandler)(void);
- (void)updateCopied:(uint64_t)copied
               total:(uint64_t)total
                file:(NSString *)file;
@end
@implementation DCPImportProgressController
- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.systemBackgroundColor;
  self.modalInPresentation = YES;
  self.label = [[UILabel alloc] init];
  self.label.text = @"Preparing game import…";
  self.label.numberOfLines = 0;
  self.label.textAlignment = NSTextAlignmentCenter;
  self.progress = [[UIProgressView alloc]
      initWithProgressViewStyle:UIProgressViewStyleDefault];
  self.cancelButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [self.cancelButton setTitle:@"Cancel import" forState:UIControlStateNormal];
  [self.cancelButton addTarget:self
                        action:@selector(cancelPressed)
              forControlEvents:UIControlEventTouchUpInside];
  UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
    self.label, self.progress, self.cancelButton
  ]];
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 20;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
    [stack.leadingAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor
                       constant:24],
    [stack.trailingAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor
                       constant:-24]
  ]];
}
- (void)cancelPressed {
  self.cancelButton.enabled = NO;
  self.label.text = @"Cancelling import…";
  if (self.cancelHandler)
    self.cancelHandler();
}
- (void)updateCopied:(uint64_t)copied
               total:(uint64_t)total
                file:(NSString *)file {
  if (!self.cancelButton.enabled)
    return;
  self.progress.progress = total ? float(double(copied) / double(total)) : 0;
  self.label.text =
      [NSString stringWithFormat:@"Importing %@\n%.2f of %.2f GB", file,
                                 double(copied) / 1e9, double(total) / 1e9];
  if (copied == total)
    self.label.text = @"Validating game data…";
}
@end

@interface DCPStorageController () {
  std::shared_ptr<std::atomic<bool>> _cancel;
}
@property(nonatomic, strong) NSURL *documentsURL;
@property(nonatomic, weak) UIViewController *presentingController;
@property(nonatomic, strong, nullable) NSFileCoordinator *coordinator;
@property(nonatomic, strong, nullable)
    DCPImportProgressController *progressController;
@property(nonatomic, readwrite, getter=isImporting) BOOL importing;
@end
@implementation DCPStorageController
- (instancetype)initWithDocumentsURL:(NSURL *)documentsURL {
  if ((self = [super init]))
    self.documentsURL = documentsURL;
  return self;
}
- (BOOL)recoverLocalStorageWithError:(NSError **)error {
  try {
    const auto root =
        std::filesystem::path(self.documentsURL.fileSystemRepresentation);
    dcp::storage::recover_import(root / "Game");
    dcp::storage::recover_save_restore(root / "ProbeSaves");
    dcp::storage::recover_checkpoints(root / "SaveCheckpoints");
    return YES;
  } catch (const std::exception &failure) {
    if (error)
      *error = storage_error(failure.what());
    return NO;
  }
}
- (void)presentGameFolderPickerFrom:(UIViewController *)controller {
  NSAssert(NSThread.isMainThread, @"Storage UI must run on main");
  if (self.importing)
    return;
  self.presentingController = controller;
  UIDocumentPickerViewController *picker =
      [[UIDocumentPickerViewController alloc]
          initForOpeningContentTypes:@[ UTTypeFolder ]
                              asCopy:NO];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  [controller presentViewController:picker animated:YES completion:nil];
}
- (void)documentPickerWasCancelled:
    (UIDocumentPickerViewController *)controller {
  if (self.completionHandler)
    self.completionHandler(
        storage_error("Import cancelled", NSUserCancelledError));
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  if (self.importing || urls.count != 1)
    return;
  self.importing = YES;
  _cancel = std::make_shared<std::atomic<bool>>(false);
  auto cancel = _cancel;
  NSURL *source = urls.firstObject;
  const BOOL scoped = [source startAccessingSecurityScopedResource];
  self.coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
  NSFileCoordinator *coordinator = self.coordinator;
  DCPImportProgressController *progress =
      [[DCPImportProgressController alloc] init];
  progress.modalPresentationStyle = UIModalPresentationFormSheet;
  __weak DCPStorageController *weakSelf = self;
  progress.cancelHandler = ^{
    [weakSelf cancelImport];
  };
  self.progressController = progress;
  void (^startWork)(void) = ^{
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
      __block NSError *failure = nil;
      NSError *coordinationError = nil;
      coordinationError =
          DCPCoordinatedRead(coordinator, source, ^(NSURL *readURL) {
            try {
              auto sourcePath =
                  std::filesystem::path(readURL.fileSystemRepresentation);
              auto destination =
                  std::filesystem::path(
                      self.documentsURL.fileSystemRepresentation) /
                  "Game";
              uint64_t lastUpdate = 0;
              dcp::storage::import_game(
                  sourcePath, destination, *cancel,
                  [self, progress,
                   &lastUpdate](const dcp::storage::Progress &state) {
                    if (state.copied != state.total &&
                        state.copied - lastUpdate < 8 * 1024 * 1024)
                      return;
                    lastUpdate = state.copied;
                    const uint64_t copied = state.copied, total = state.total;
                    NSString *file = @(state.file.c_str());
                    dispatch_async(dispatch_get_main_queue(), ^{
                      [progress updateCopied:copied total:total file:file];
                      if (self.progressHandler)
                        self.progressHandler(copied, total, file);
                    });
                  });
            } catch (const std::exception &error) {
              failure = storage_error(
                  error.what(), cancel->load() ? NSUserCancelledError : 1);
            }
          });
      if (scoped)
        [source stopAccessingSecurityScopedResource];
      if (!failure)
        failure = coordinationError;
      dispatch_async(dispatch_get_main_queue(), ^{
        self.importing = NO;
        self.coordinator = nil;
        self.progressController = nil;
        [progress dismissViewControllerAnimated:YES
                                     completion:^{
                                       if (self.completionHandler)
                                         self.completionHandler(failure);
                                     }];
      });
    });
  };
  // Start work only after progress is visible, including fast preflight errors.
  [controller dismissViewControllerAnimated:YES
                                 completion:^{
                                   [self.presentingController
                                       presentViewController:progress
                                                    animated:YES
                                                  completion:startWork];
                                 }];
}
- (void)cancelImport {
  NSAssert(NSThread.isMainThread, @"Storage UI must run on main");
  if (_cancel)
    _cancel->store(true);
  [self.coordinator cancel];
}
@end
