// Simulator-only Files picker QA app with synthetic local saves.
#import <UIKit/UIKit.h>
#import "save-transfer-ui.h"
@interface FilesQAController : UIViewController
@property(nonatomic, strong) DCPSaveTransferController *transfer;
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) NSURL *documents;
@property(nonatomic) unsigned errorCount;
@end
@implementation FilesQAController
- (void)viewDidLoad {
  [super viewDidLoad]; self.view.backgroundColor = UIColor.systemBackgroundColor;
  self.documents = [NSURL fileURLWithPath:NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject isDirectory:YES];
  for (NSArray<NSString *> *item in @[@[@"ProbeSaves/SAVEDATA00/SAVE0.DAT", @"before"], @[@"Incoming/SAVEDATA00/SAVE0.DAT", @"after"], @[@"protected.txt", @"marker"]]) {
    NSURL *url = [self.documents URLByAppendingPathComponent:item[0]];
    [NSFileManager.defaultManager createDirectoryAtURL:url.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    [item[1] writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:nil];
  }
  self.transfer = [[DCPSaveTransferController alloc] initWithDocumentsURL:self.documents];
  [self.transfer setValue:self.documents forKey:@"lastSaveFolder"];
  self.status = [[UILabel alloc] init]; self.status.text = @"Ready"; self.status.accessibilityIdentifier = @"transfer-status"; self.status.numberOfLines = 0;
  UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
  [button setTitle:@"Import Save" forState:UIControlStateNormal]; button.accessibilityIdentifier = @"import-save";
  [button addTarget:self action:@selector(importSave) forControlEvents:UIControlEventTouchUpInside];
  UIButton *exportButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [exportButton setTitle:@"Export Save" forState:UIControlStateNormal]; exportButton.accessibilityIdentifier = @"export-save";
  [exportButton addTarget:self action:@selector(exportSave) forControlEvents:UIControlEventTouchUpInside];
  UIButton *changeButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [changeButton setTitle:@"Change Synthetic Save" forState:UIControlStateNormal]; changeButton.accessibilityIdentifier = @"change-save";
  [changeButton addTarget:self action:@selector(changeSave) forControlEvents:UIControlEventTouchUpInside];
  UIButton *corruptButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [corruptButton setTitle:@"Corrupt Export" forState:UIControlStateNormal]; corruptButton.accessibilityIdentifier = @"corrupt-export";
  [corruptButton addTarget:self action:@selector(corruptExport) forControlEvents:UIControlEventTouchUpInside];
  UIButton *repairButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [repairButton setTitle:@"Repair Export" forState:UIControlStateNormal]; repairButton.accessibilityIdentifier = @"repair-export";
  [repairButton addTarget:self action:@selector(repairExport) forControlEvents:UIControlEventTouchUpInside];
  UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[button,exportButton,changeButton,corruptButton,repairButton,self.status]]; stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 20; stack.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[[stack.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],[stack.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor]]];
  __weak FilesQAController *weakSelf = self;
  self.transfer.completionHandler = ^(NSString *message, NSError *error) {
    FilesQAController *strongSelf = weakSelf;
    if (error) {
      strongSelf.status.text = error.localizedDescription;
      ++strongSelf.errorCount;
      NSString *value = [NSString stringWithContentsOfURL:[strongSelf.documents URLByAppendingPathComponent:@"ProbeSaves/SAVEDATA00/SAVE0.DAT"] encoding:NSUTF8StringEncoding error:nil];
      NSString *record = [NSString stringWithFormat:@"%@\n%@", error.localizedDescription, value ?: @"<missing>"];
      [record writeToURL:[strongSelf.documents URLByAppendingPathComponent:[NSString stringWithFormat:@"qa-error-%u.txt",strongSelf.errorCount]] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }
    else if ([message hasPrefix:@"Save copy exported."]) {
      NSURL *exported = [strongSelf.transfer valueForKey:@"lastSaveFolder"];
      NSString *name = exported.lastPathComponent ?: @"";
      [name writeToURL:[strongSelf.documents URLByAppendingPathComponent:@"qa-export-name.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
      [exported.path writeToURL:[strongSelf.documents URLByAppendingPathComponent:@"qa-export-path.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
      strongSelf.status.text = [@"EXPORTED:" stringByAppendingString:name];
    } else strongSelf.status.text = message;
  };
}
- (void)importSave { [self.transfer importFromFiles:self]; }
- (void)exportSave { [self.transfer exportToFiles:self]; }
- (void)writeExport:(NSString *)value {
  NSURL *exported = [self.transfer valueForKey:@"lastSaveFolder"];
  NSString *prefix = [self.documents.path stringByAppendingString:@"/"];
  if (self.transfer.isBusy || ![exported.path hasPrefix:prefix]) {
    self.status.text = @"Invalid QA export location"; return;
  }
  BOOL ok = [value writeToURL:[exported URLByAppendingPathComponent:@"SAVEDATA00/SAVE0.DAT"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
  self.status.text = ok ? ([value isEqualToString:@"before"] ? @"Export repaired" : @"Export corrupted") : @"Could not alter QA export";
}
- (void)corruptExport { [self writeExport:@"badbad"]; }
- (void)repairExport { [self writeExport:@"before"]; }
- (void)changeSave {
  if (self.transfer.isBusy) return;
  [@"after" writeToURL:[self.documents URLByAppendingPathComponent:@"ProbeSaves/SAVEDATA00/SAVE0.DAT"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
  self.status.text = @"Synthetic save changed";
}
@end
@interface FilesQAScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation FilesQAScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  (void)session; (void)options; self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene]; self.window.rootViewController = [[FilesQAController alloc] init]; [self.window makeKeyAndVisible];
}
@end
@interface FilesQA : UIResponder <UIApplicationDelegate> @end
@implementation FilesQA
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  (void)app; (void)options; UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"QA" sessionRole:session.role]; config.delegateClass = FilesQAScene.class; return config;
}
@end
int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(FilesQA.class)); } }
