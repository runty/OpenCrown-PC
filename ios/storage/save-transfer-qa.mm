// Standalone Simulator app: all saves and checkpoints are synthetic and local.
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "save-transfer-ui.h"
#include "storage-core.h"

static NSDictionary<NSString *, NSData *> *fileInventory(NSURL *root) {
  NSMutableDictionary<NSString *, NSData *> *result = [NSMutableDictionary dictionary];
  NSDirectoryEnumerator<NSURL *> *items = [NSFileManager.defaultManager enumeratorAtURL:root
      includingPropertiesForKeys:@[NSURLIsRegularFileKey] options:0 errorHandler:nil];
  for (NSURL *url in items) {
    NSNumber *regular = nil;
    if (![url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil])
      @throw [NSException exceptionWithName:@"TransferQA" reason:@"Cannot inspect checkpoint" userInfo:nil];
    if (regular.boolValue) {
      NSData *bytes = [NSData dataWithContentsOfURL:url];
      if (!bytes) @throw [NSException exceptionWithName:@"TransferQA" reason:@"Cannot read checkpoint" userInfo:nil];
      result[url.path] = bytes;
    }
  }
  return result;
}
@interface TransferQAScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation TransferQAScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  (void)session; (void)options;
  self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
  self.window.rootViewController = [[UIViewController alloc] init];
  [self.window makeKeyAndVisible];
}
@end
@interface TransferQA : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) DCPSaveTransferController *transfer;
@property(nonatomic, strong) NSURL *root;
@property(nonatomic) unsigned checks;
@end
@implementation TransferQA
- (void)check:(BOOL)condition message:(NSString *)message {
  if (!condition) @throw [NSException exceptionWithName:@"TransferQA" reason:message userInfo:nil];
  ++self.checks;
}
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  (void)app; (void)options;
  UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"QA" sessionRole:session.role];
  config.delegateClass = TransferQAScene.class;
  return config;
}
- (void)writeText:(NSString *)text relative:(NSString *)path {
  NSURL *url = [self.root URLByAppendingPathComponent:path]; NSError *error = nil;
  BOOL made = [NSFileManager.defaultManager createDirectoryAtURL:url.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error];
  [self check:made && !error message:@"Could not create synthetic save folder"];
  BOOL wrote = [text writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:&error];
  [self check:wrote && !error message:@"Could not write synthetic save"];
}
- (NSString *)read:(NSString *)path {
  return [NSString stringWithContentsOfURL:[self.root URLByAppendingPathComponent:path] encoding:NSUTF8StringEncoding error:nil];
}
- (void)select:(NSURL *)url {
  UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
  [self.transfer setValue:picker forKey:@"picker"];
  [self.transfer setValue:@YES forKey:@"busy"];
  [self.transfer setValue:@NO forKey:@"working"];
  [self.transfer setValue:@NO forKey:@"exporting"];
  [self.transfer documentPicker:picker didPickDocumentsAtURLs:@[url]];
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
  (void)application; (void)options;
  NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  self.root = [NSURL fileURLWithPath:documents isDirectory:YES];
  [self writeText:@"previous-save" relative:@"ProbeSaves/SAVEDATA00/SAVE0.DAT"];
  [self writeText:@"incoming-save" relative:@"Incoming/SAVEDATA00/SAVE0.DAT"];
  [self writeText:@"marker" relative:@"protected.txt"];
  [self writeText:@"wrong-folder" relative:@"Invalid/not-a-save.txt"];
  self.transfer = [[DCPSaveTransferController alloc] initWithDocumentsURL:self.root];
  self.transfer.completionHandler = ^(NSString *message, NSError *error) {
    [self check:NSThread.isMainThread && !self.transfer.isBusy message:@"Completion did not release on main"];
    [self check:!error && message != nil message:@"Valid local transfer failed"];
    [self check:[[self read:@"ProbeSaves/SAVEDATA00/SAVE0.DAT"] isEqualToString:@"incoming-save"] message:@"Incoming save not installed"];
    [self check:[[self read:@"Incoming/SAVEDATA00/SAVE0.DAT"] isEqualToString:@"incoming-save"] message:@"Source was changed"];
    NSURL *backups = [self.root URLByAppendingPathComponent:@"SaveCheckpoints" isDirectory:YES];
    NSArray<NSURL *> *entries = [NSFileManager.defaultManager contentsOfDirectoryAtURL:backups includingPropertiesForKeys:nil options:0 error:nil];
    BOOL foundPrevious = NO, foundIncoming = NO;
    NSURL *previous = nil;
    for (NSURL *entry in entries) {
      NSString *value = [NSString stringWithContentsOfURL:[entry URLByAppendingPathComponent:@"SAVEDATA00/SAVE0.DAT"] encoding:NSUTF8StringEncoding error:nil];
      if ([value isEqualToString:@"previous-save"]) { foundPrevious = YES; previous = entry; }
      foundIncoming |= [value isEqualToString:@"incoming-save"];
    }
    [self check:foundPrevious && foundIncoming message:@"Pre-import or incoming checkpoint missing"];
    bool valid = true;
    try { dcp::storage::validate_checkpoint(previous.fileSystemRepresentation); }
    catch (const std::exception &) { valid = false; }
    [self check:valid message:@"Previous checkpoint failed validation"];
    bool restored = true;
    try { dcp::storage::restore_checkpoint(previous.fileSystemRepresentation,
      [self.root URLByAppendingPathComponent:@"Recovered"].fileSystemRepresentation); }
    catch (const std::exception &) { restored = false; }
    [self check:restored && [[self read:@"Recovered/SAVEDATA00/SAVE0.DAT"] isEqualToString:@"previous-save"] message:@"Previous checkpoint could not be restored"];
    [self check:[[self read:@"ProbeSaves/SAVEDATA00/SAVE0.DAT"] isEqualToString:@"incoming-save"] message:@"Separate restore changed current save"];
    NSArray *before = [entries valueForKey:@"lastPathComponent"];
    NSDictionary *beforeBytes = fileInventory(backups);
    self.transfer.completionHandler = ^(NSString *nextMessage, NSError *nextError) {
      [self check:NSThread.isMainThread && !self.transfer.isBusy message:@"Failed import did not release"];
      [self check:nextError != nil && nextMessage == nil message:@"Invalid folder did not fail"];
      [self check:[[self read:@"ProbeSaves/SAVEDATA00/SAVE0.DAT"] isEqualToString:@"incoming-save"] message:@"Failed import changed current save"];
      [self check:[[self read:@"protected.txt"] isEqualToString:@"marker"] message:@"Protected marker changed"];
      NSArray *after = [NSFileManager.defaultManager contentsOfDirectoryAtPath:backups.path error:nil];
      [self check:[[NSSet setWithArray:before] isEqualToSet:[NSSet setWithArray:after]] message:@"Failed import changed checkpoints"];
      [self check:[beforeBytes isEqualToDictionary:fileInventory(backups)] message:@"Failed import changed checkpoint bytes"];
      NSString *result = [NSString stringWithFormat:@"{\"checks\":%u,\"failed\":0}\n", self.checks];
      [result writeToURL:[self.root URLByAppendingPathComponent:@"picker-results.json"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    };
    [self select:[self.root URLByAppendingPathComponent:@"Invalid" isDirectory:YES]];
  };
  [self select:[self.root URLByAppendingPathComponent:@"Incoming" isDirectory:YES]];
  return YES;
}
@end
int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(TransferQA.class)); } }
