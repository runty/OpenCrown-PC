// Standalone simulator QA app. No game, provider, or normal save is used.
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "storage-ui.h"
#import "save-transfer-ui.h"

@interface DeferredPicker : UIDocumentPickerViewController
@property(nonatomic, copy) void (^dismissal)(void);
@end
@implementation DeferredPicker
- (void)dismissViewControllerAnimated:(BOOL)animated completion:(void (^)(void))completion {
  (void)animated; self.dismissal = completion;
}
@end
@interface AttachedView : UIView
@property(nonatomic, strong) UIWindow *fakeWindow;
@end
@implementation AttachedView
- (UIWindow *)window { return self.fakeWindow; }
@end
@interface DeferredPresenter : UIViewController
@property(nonatomic, copy) void (^presentation)(void);
@end
@implementation DeferredPresenter
- (void)presentViewController:(UIViewController *)controller animated:(BOOL)animated completion:(void (^)(void))completion {
  (void)controller; (void)animated; self.presentation = completion;
}
@end
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
  DeferredPicker *delayed = [[DeferredPicker alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
  [storage setValue:delayed forKey:@"picker"];
  [storage documentPicker:delayed didPickDocumentsAtURLs:@[root]];
  check(storage.isImporting && [storage valueForKey:@"pendingStart"] != nil, @"Expected pending dismissal");
  [storage cancelImport];
  check(!storage.isImporting && gameCompletions == 4 && gameError.code == NSUserCancelledError, @"Pending dismissal cancellation stuck");
  active = picker(); [storage setValue:active forKey:@"picker"];
  delayed.dismissal();
  check(!storage.isImporting && gameCompletions == 4 && [storage valueForKey:@"picker"] == active, @"Late dismissal affected a new session");
  check(![[NSFileManager defaultManager] fileExistsAtPath:[documents stringByAppendingPathComponent:@"Game"]], @"Cancelled import touched game data");
  [storage cancelImport];
  check(gameCompletions == 5, @"New session failed to cancel");

  DeferredPresenter *presenter = [[DeferredPresenter alloc] init];
  AttachedView *attached = [[AttachedView alloc] init];
  attached.fakeWindow = [[UIWindow alloc] initWithFrame:CGRectMake(0,0,100,100)];
  presenter.view = attached;
  [storage setValue:presenter forKey:@"presentingController"];
  delayed = [[DeferredPicker alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
  [storage setValue:delayed forKey:@"picker"];
  [storage documentPicker:delayed didPickDocumentsAtURLs:@[root]];
  delayed.dismissal();
  check(storage.isImporting && presenter.presentation != nil, @"Expected pending presentation");
  [storage cancelImport];
  check(!storage.isImporting && gameCompletions == 6, @"Pending presentation cancellation stuck");
  active = picker(); [storage setValue:active forKey:@"picker"];
  presenter.presentation();
  check(!storage.isImporting && gameCompletions == 6 && [storage valueForKey:@"picker"] == active, @"Late presentation affected a new session");
  check(![[NSFileManager defaultManager] fileExistsAtPath:[documents stringByAppendingPathComponent:@"Game"]], @"Late presentation started a worker");
  [storage cancelImport];
  check(gameCompletions == 7, @"New session after presentation failed to cancel");
  check([[NSString stringWithContentsOfURL:marker encoding:NSUTF8StringEncoding error:nil] isEqualToString:@"unchanged"], @"Marker changed");
  [storage setValue:nil forKey:@"presentingController"];
  delayed = [[DeferredPicker alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
  [storage setValue:delayed forKey:@"picker"];
  [storage documentPicker:delayed didPickDocumentsAtURLs:@[root]];
  delayed.dismissal();
  check(!storage.isImporting && gameCompletions == 8 && gameError.code == NSUserCancelledError, @"Detached presenter did not cancel pending import");
  check(![[NSFileManager defaultManager] fileExistsAtPath:[documents stringByAppendingPathComponent:@"Game"]], @"Detached presenter started a worker");
  [storage setValue:presenter forKey:@"presentingController"];
  // Complete a real local coordinated read through the worker path. The
  // folder deliberately lacks game data and must fail without a destination.
  storage.completionHandler = ^(NSError *error) {
    check(error != nil && !storage.isImporting, @"Failed local import did not finish");
    check(![[NSFileManager defaultManager] fileExistsAtPath:[documents stringByAppendingPathComponent:@"Game"]], @"Invalid local source created game data");
    check([[NSString stringWithContentsOfURL:marker encoding:NSUTF8StringEncoding error:nil] isEqualToString:@"unchanged"], @"Local import changed marker");
    NSString *result = [NSString stringWithFormat:@"{\"checks\":%u,\"failed\":0}\n", checks];
    [result writeToURL:[root URLByAppendingPathComponent:@"picker-results.json"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
  };
  delayed = [[DeferredPicker alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
  [storage setValue:delayed forKey:@"picker"];
  [storage documentPicker:delayed didPickDocumentsAtURLs:@[root]];
  delayed.dismissal();
  presenter.presentation();
  check(storage.isImporting, @"Local import worker failed to start");
  return YES;
}
@end
int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(PickerQA.class)); } }
