#import <XCTest/XCTest.h>
@interface FilesTests : XCTestCase @end
@implementation FilesTests
- (void)selectExport:(NSString *)folderName inApp:(XCUIApplication *)app {
  [app.buttons[@"import-save"] tap];
  NSString *insideLabel = [folderName stringByAppendingString:@", Actions Menu"];
  XCUIElement *inside = app.buttons[insideLabel];
  if (![inside waitForExistenceWithTimeout:5]) {
    XCUIElement *folder = app.staticTexts[folderName];
    if (![folder waitForExistenceWithTimeout:15]) {
      NSLog(@"PICKER TREE: %@", app.debugDescription);
      XCTFail(@"Exported local folder not visible in import picker"); return;
    }
    [[folder coordinateWithNormalizedOffset:CGVectorMake(0.5,0.5)] tap];
    XCTAssertTrue([inside waitForExistenceWithTimeout:10]);
  }
}
- (void)testExportAndImportLocalFolder {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] init]; [app launch];
  XCTAssertTrue([app.buttons[@"export-save"] waitForExistenceWithTimeout:20]);
  [app.buttons[@"export-save"] tap];
  XCUIElement *save = app.buttons[@"Save"];
  XCTAssertTrue([save waitForExistenceWithTimeout:20]);
  // Export only into this unique QA app's local Documents container.
  NSString *containerName = app.label;
  XCUIElement *ownTitle = app.buttons[[containerName stringByAppendingString:@", Actions Menu"]];
  XCUIElement *back = app.buttons[@"DOC.navBarButton.backInHistory"];
  if (!(ownTitle.exists && ownTitle.hittable)) {
    XCUIElement *ownCell = app.cells[[containerName stringByAppendingString:@", Container"]];
    for (unsigned i = 0; i < 4 && !ownCell.exists; ++i) {
      if (!back.exists || !back.enabled) break;
      [back tap];
    }
    if ([ownCell waitForExistenceWithTimeout:5])
      [[ownCell coordinateWithNormalizedOffset:CGVectorMake(0.5,0.5)] tap];
  }
  if (![ownTitle waitForExistenceWithTimeout:8]) NSLog(@"EXPORT PICKER TREE: %@", app.debugDescription);
  XCTAssertTrue(ownTitle.exists && ownTitle.hittable, @"Refusing to export outside this QA app's local container");
  [save tap];
  XCUIElement *status = app.staticTexts[@"transfer-status"];
  NSPredicate *exported = [NSPredicate predicateWithFormat:@"label BEGINSWITH %@", @"EXPORTED:"];
  XCTAssertTrue([[app.staticTexts matchingPredicate:exported].firstMatch waitForExistenceWithTimeout:30]);
  NSString *label = status.label;
  NSString *folderName = [label substringFromIndex:[@"EXPORTED:" length]];
  XCTAssertEqual(folderName.length, 32u);
  [app.buttons[@"change-save"] tap];
  XCTAssertTrue([app.staticTexts[@"Synthetic save changed"] waitForExistenceWithTimeout:10]);
  [self selectExport:folderName inApp:app];
  XCUIElement *cancel = app.buttons[@"Cancel"];
  for (unsigned i = 0; i < 4 && !cancel.exists; ++i) {
    if (!back.exists || !back.enabled) break;
    [back tap];
  }
  if (![cancel waitForExistenceWithTimeout:5]) NSLog(@"CANCEL TREE: %@", app.debugDescription);
  XCTAssertTrue(cancel.exists);
  [cancel tap];
  XCTAssertTrue([app.staticTexts[@"Save transfer cancelled"] waitForExistenceWithTimeout:15]);

  [app.buttons[@"corrupt-export"] tap];
  XCTAssertTrue([app.staticTexts[@"Export corrupted"] waitForExistenceWithTimeout:10]);
  [self selectExport:folderName inApp:app];
  XCUIElement *open = app.buttons[@"Open"];
  XCTAssertTrue([open waitForExistenceWithTimeout:15]); [open tap];
  NSPredicate *damaged = [NSPredicate predicateWithFormat:@"label CONTAINS %@", @"File checksum mismatch"];
  XCTAssertTrue([[app.staticTexts matchingPredicate:damaged].firstMatch waitForExistenceWithTimeout:30]);

  [app.buttons[@"repair-export"] tap];
  XCTAssertTrue([app.staticTexts[@"Export repaired"] waitForExistenceWithTimeout:10]);
  [self selectExport:folderName inApp:app];
  XCTAssertTrue([open waitForExistenceWithTimeout:15]); [open tap];
  NSPredicate *success = [NSPredicate predicateWithFormat:@"label CONTAINS %@", @"Save imported."];
  XCTAssertTrue([[app.staticTexts matchingPredicate:success].firstMatch waitForExistenceWithTimeout:45]);
  [app terminate];
}
@end
