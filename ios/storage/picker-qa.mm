// Standalone simulator QA app. No game, provider, or normal save is used.
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "storage-ui.h"
#import "save-transfer-ui.h"

@interface PickerQAScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation PickerQAScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  (void)session; (void)options;
  self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
  self.window.rootViewController = [[UIViewController alloc] init];
  [self.window makeKeyAndVisible];
}
@end
@interface PickerQA : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation PickerQA
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  (void)app; (void)options;
  UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"QA" sessionRole:session.role];
  config.delegateClass = PickerQAScene.class;
  return config;
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
  (void)application; (void)options;
  __block unsigned checks = 0;
  void (^check)(BOOL, NSString *) = ^(BOOL condition, NSString *message) {
    if (!condition) @throw [NSException exceptionWithName:@"PickerQA" reason:message userInfo:nil];
    ++checks;
  };
  NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  NSURL *root = [NSURL fileURLWithPath:documents isDirectory:YES];
  NSURL *marker = [root URLByAppendingPathComponent:@"protected.txt"];
  [@"unchanged" writeToURL:marker atomically:YES encoding:NSUTF8StringEncoding error:nil];
  UIDocumentPickerViewController *(^picker)(void) = ^{
    return [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ] asCopy:NO];
  };
  DCPSaveTransferController *save = [[DCPSaveTransferController alloc] initWithDocumentsURL:root];
  __block unsigned saveCompletions = 0;
  __block NSError *saveError = nil;
  save.completionHandler = ^(NSString *message, NSError *error) {
    check(message == nil, @"Unexpected save success");
    ++saveCompletions; saveError = error;
  };
  UIDocumentPickerViewController *active = picker(), *stale = picker();
  [save setValue:active forKey:@"picker"]; [save setValue:@YES forKey:@"busy"];
  [save documentPicker:stale didPickDocumentsAtURLs:@[]];
  check(save.isBusy && saveCompletions == 0, @"Stale save picker affected session");
  [save documentPicker:active didPickDocumentsAtURLs:@[]];
  check(!save.isBusy && saveCompletions == 1 && saveError != nil, @"Empty save result was not failed");
  [save documentPickerWasCancelled:active];
  check(saveCompletions == 1, @"Duplicate save callback completed twice");
  active = picker(); [save setValue:active forKey:@"picker"]; [save setValue:@YES forKey:@"busy"];
  [save setValue:@YES forKey:@"working"];
  [save documentPickerWasCancelled:active];
  check(save.isBusy && saveCompletions == 1, @"Cancel callback released an active transaction");
  [save setValue:@NO forKey:@"working"]; [save cancel];
  check(!save.isBusy && saveCompletions == 2, @"Cancelled save session did not finish");
  active = picker(); [save setValue:active forKey:@"picker"]; [save setValue:@YES forKey:@"busy"];
  [save documentPicker:active didPickDocumentsAtURLs:@[root, marker]];
  check(!save.isBusy && saveCompletions == 3 && saveError != nil, @"Multiple save URLs were accepted");

  DCPStorageController *storage = [[DCPStorageController alloc] initWithDocumentsURL:root];
  __block unsigned gameCompletions = 0;
  __block NSError *gameError = nil;
  storage.completionHandler = ^(NSError *error) { ++gameCompletions; gameError = error; };
  active = picker(); stale = picker(); [storage setValue:active forKey:@"picker"];
  [storage documentPicker:stale didPickDocumentsAtURLs:@[]];
  check(gameCompletions == 0 && [storage valueForKey:@"picker"] == active, @"Stale game picker affected session");
  [storage documentPicker:active didPickDocumentsAtURLs:@[]];
  check(gameCompletions == 1 && gameError && !storage.isImporting && ![storage valueForKey:@"picker"], @"Empty game selection was not failed");
  [storage documentPickerWasCancelled:active];
  check(gameCompletions == 1, @"Duplicate game callback completed twice");
  active = picker(); [storage setValue:active forKey:@"picker"]; [storage cancelImport];
  check(gameCompletions == 2 && gameError.code == NSUserCancelledError, @"Scene cancellation did not finish picker");
  [storage documentPicker:active didPickDocumentsAtURLs:@[root]];
  check(gameCompletions == 2 && !storage.isImporting, @"Cancelled picker started a late import");
  active = picker(); [storage setValue:active forKey:@"picker"];
  [storage documentPicker:active didPickDocumentsAtURLs:@[root, marker]];
  check(gameCompletions == 3 && gameError && !storage.isImporting, @"Multiple game URLs were accepted");
  check([[NSString stringWithContentsOfURL:marker encoding:NSUTF8StringEncoding error:nil] isEqualToString:@"unchanged"], @"Marker changed");
  NSString *result = [NSString stringWithFormat:@"{\"checks\":%u,\"failed\":0}\n", checks];
  [result writeToURL:[root URLByAppendingPathComponent:@"picker-results.json"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
  return YES;
}
@end
int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(PickerQA.class)); } }
