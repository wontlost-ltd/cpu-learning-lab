import XCTest
final class PhoneUITests:XCTestCase {
    func testUnpairedLaunchDoesNotEnableCaptureOrSync() {
        let app=XCUIApplication();app.launch()
        XCTAssertTrue(app.buttons["importPairing"].waitForExistence(timeout:10))
        XCTAssertFalse(app.buttons["captureButton"].isEnabled)
        XCTAssertFalse(app.buttons["syncButton"].isEnabled)
        let status=app.staticTexts["statusText"]
        for _ in 0..<3 where !status.exists { app.swipeUp() }
        XCTAssertTrue(status.waitForExistence(timeout:5))
    }
}
