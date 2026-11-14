#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <sys/utsname.h>
#import <unistd.h>

@interface ProbeController : UIViewController
@end
@implementation ProbeController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.title = @"OpenCrown Device Probe";
    NSDictionary *info = NSBundle.mainBundle.infoDictionary;
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *path = [documents stringByAppendingPathComponent:@"diagnostics.json"];
    NSDictionary *previous = [NSDictionary dictionaryWithContentsOfFile:[documents stringByAppendingPathComponent:@"launches.plist"]];
    NSUInteger launches = [previous[@"count"] unsignedIntegerValue] + 1;
    [@{@"count": @(launches)} writeToFile:[documents stringByAppendingPathComponent:@"launches.plist"] atomically:YES];
    struct utsname machine;
    uname(&machine);
    id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
    NSURL *url = [NSURL fileURLWithPath:documents];
    NSNumber *available = nil;
    [url getResourceValue:&available forKey:NSURLVolumeAvailableCapacityForImportantUsageKey error:nil];
    NSDictionary *diagnostic = @{
        @"build_id": info[@"DCPBuildID"] ?: @"unknown",
        @"stage": @"P0 device shell",
        @"guest_execution": @NO,
        @"architecture": @"arm64",
        @"machine": @(machine.machine),
        @"os": NSProcessInfo.processInfo.operatingSystemVersionString,
        @"page_size": @(getpagesize()),
        @"available_bytes": available ?: @0,
        @"launch_count": @(launches),
        @"timestamp": @([[NSDate date] timeIntervalSince1970]),
        @"metal_device": gpu.name ?: @"unavailable",
        @"bc_texture_compression": @(gpu.supportsBCTextureCompression)
    };
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:diagnostic options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
    BOOL saved = data && [data writeToFile:path options:NSDataWritingAtomic error:&error];
    UITextView *text = [[UITextView alloc] init];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];
    text.text = saved ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : error.localizedDescription;
    text.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:text];
    [NSLayoutConstraint activateConstraints:@[
        [text.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [text.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [text.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [text.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12]
    ]];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAction target:self action:@selector(exportLog:)];
    NSLog(@"OpenCrown P0 diagnostics saved=%d %@", saved, text.text);
}
- (void)exportLog:(UIBarButtonItem *)sender {
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSURL *url = [NSURL fileURLWithPath:[documents stringByAppendingPathComponent:@"diagnostics.json"]];
    UIActivityViewController *sheet = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
    sheet.popoverPresentationController.barButtonItem = sender;
    [self presentViewController:sheet animated:YES completion:nil];
}
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:[[ProbeController alloc] init]];
    [self.window makeKeyAndVisible];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AppDelegate
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Probe" sessionRole:session.role];
    config.delegateClass = SceneDelegate.class;
    return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class));
    }
}
