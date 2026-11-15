#pragma once
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN
// Own exclusively while ALL guest workers/writers are stopped. Transfers are
// explicit copies. No persistent cloud permissions or automatic sync.
@interface DCPSaveTransferController : NSObject <UIDocumentPickerDelegate>
- (instancetype)initWithDocumentsURL:(NSURL *)documentsURL;
@property(nonatomic, readonly, getter=isBusy) BOOL busy;
@property(nonatomic, copy, nullable) void (^completionHandler)
    (NSString *_Nullable message, NSError *_Nullable error);
- (void)importFromFiles:(UIViewController *)presenter;
- (void)exportToFiles:(UIViewController *)presenter;
- (void)cancel;
@end
NS_ASSUME_NONNULL_END
