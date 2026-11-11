import XCTest
@testable import PhoneCore
import CPUBridge
final class PhoneCoreTests:XCTestCase {
    func testPairingStrictBoundsAndIdentity() throws {
        let data=Data("CPUAI_PAIR1 desktop phone \(String(repeating:"a",count:64)) \(String(repeating:"b",count:64))\n".utf8)
        let pair=try Pairing(data:data);XCTAssertEqual(pair.server,"desktop");XCTAssertEqual(try Pairing(data:pair.encoded),pair)
        XCTAssertThrowsError(try Pairing(data:data+Data("extra".utf8)))
        XCTAssertThrowsError(try Pairing(data:Data(repeating:65,count:1025)))
        XCTAssertFalse(Pairing.validID("../phone"));XCTAssertFalse(Pairing.validID("设备"));XCTAssertTrue(Pairing.validID("phone-1"))
    }
    func testBridgeCapturePersistsAndRestarts() async throws {
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true);defer { try? FileManager.default.removeItem(at:directory) }
        let path=directory.appendingPathComponent("phone.journal")
        do {
            let store=try PhoneStore(path:path,device:"phone")
            let capture=try await store.startCapture(rate:16000) { _ in }
            let pcm=(0..<1600).map { Float(0.5*sin(2*Double.pi*500*Double($0)/16000)) }
            pcm.withUnsafeBufferPointer { XCTAssertTrue(capture.push(left:$0.baseAddress!,right:nil,frames:$0.count)) }
            let stats=try await store.finishCapture();XCTAssertEqual(stats?.committed,1)
            let count=try await store.count();XCTAssertEqual(count,1);try await store.close()
        }
        let restored=try PhoneStore(path:path,device:"phone");let count=try await restored.count();XCTAssertEqual(count,1);try await restored.close()
    }
}
