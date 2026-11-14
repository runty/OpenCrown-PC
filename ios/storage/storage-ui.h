#pragma once
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN
// Retain this object while its picker/copy operation is in flight. Call on
// main. Integrator must stop the guest and all its workers before presenting
// import, recovering transactions, or checkpointing/restoring saves through
// storage-core.h.
@interface DCPStorageController : NSObject <UIDocumentPickerDelegate>
- (instancetype)initWithDocumentsURL:(NSURL *)documentsURL;
@property(nonatomic, readonly, getter=isImporting) BOOL importing;
@property(nonatomic, copy, nullable) void (^progressHandler)
    (uint64_t copied, uint64_t total, NSString *file);
@property(nonatomic, copy, nullable) void (^completionHandler)
    (NSError *_Nullable error);
- (void)presentGameFolderPickerFrom:(UIViewController *)controller;
- (void)cancelImport;
// Fast transaction recovery only, no CPK scanning. Call before starting a
// guest.
- (BOOL)recoverLocalStorageWithError:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
