import XCTest
final class PhoneUITests:XCTestCase {
    func testUnpairedLaunchDoesNotEnableCaptureOrSync() {
        let app=XCUIApplication();app.launch()
        XCTAssertTrue(app.buttons["importPairing"].waitForExistence(timeout:10))
        XCTAssertFalse(app.buttons["captureButton"].isEnabled)
        XCTAssertFalse(app.buttons["syncButton"].isEnabled)
        XCTAssertTrue(app.staticTexts["statusText"].exists)
    }
}
