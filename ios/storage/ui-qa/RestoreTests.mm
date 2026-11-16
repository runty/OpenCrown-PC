#import <XCTest/XCTest.h>
@interface RestoreTests : XCTestCase
@end
@implementation RestoreTests
// Each method requires its own pre-seeded, independent QA app container.
// Run one method per invocation using xcodebuild -only-testing.
- (void)testInterruptedSaveThenRestore {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] init];
  [app launch];
  XCUIElement *restore = app.buttons[@"Restore Save Checkpoint"];
  XCTAssertTrue([restore waitForExistenceWithTimeout:30]);
  [restore tap];
  XCUIElement *sheet = app.sheets.firstMatch;
  XCTAssertTrue([sheet waitForExistenceWithTimeout:20]);
  XCUIElement *checkpoint = [sheet.buttons elementBoundByIndex:0];
  XCTAssertTrue(checkpoint.exists);
  XCTAssertNotEqualObjects(checkpoint.label, @"Cancel");
  [checkpoint tap];
  XCTAssertTrue([app.staticTexts[@"Save checkpoint restored."] waitForExistenceWithTimeout:30]);
  [app terminate];
}
- (void)testCorruptRollbackPreservesSave {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] init];
  [app launch];
  XCUIElement *restore = app.buttons[@"Restore Save Checkpoint"];
  XCTAssertTrue([restore waitForExistenceWithTimeout:30]);
  [restore tap];
  XCUIElement *sheet = app.sheets.firstMatch;
  XCTAssertTrue([sheet waitForExistenceWithTimeout:20]);
  XCUIElement *checkpoint = [sheet.buttons elementBoundByIndex:0];
  XCTAssertTrue(checkpoint.exists);
  XCTAssertNotEqualObjects(checkpoint.label, @"Cancel");
  [checkpoint tap];
  NSPredicate *failure = [NSPredicate predicateWithFormat:@"label CONTAINS %@", @"File checksum mismatch"];
  XCTAssertTrue([[app.staticTexts matchingPredicate:failure].firstMatch waitForExistenceWithTimeout:30]);
  [app terminate];
}
@end
