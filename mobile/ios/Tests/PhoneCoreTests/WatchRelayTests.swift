import XCTest
@testable import PhoneCore
final class WatchRelayTests:XCTestCase {
    func testMetadataOnlyConfigurationAndStrictEnvelopes() throws {
        let identity=try WatchIdentity(device:"watch",phone:"phone")
        XCTAssertEqual(try WatchIdentity.fromOffer(identity.offer),identity)
        XCTAssertEqual(identity.offer.count,3);XCTAssertNil(identity.offer["token"]);XCTAssertNil(identity.offer["pin"])
        XCTAssertThrowsError(try WatchIdentity(device:"watch",phone:"watch"))
        let batch=try RelayBatch(identity:identity,payload:Data("sample".utf8),count:1)
        XCTAssertEqual(try batch.verify(batch.ack(inserted:1)),1)
        var message=batch.message;message["count"]=true
        XCTAssertThrowsError(try RelayBatch.decode(message,identity:identity))
        message=batch.message;message["device"]="different"
        XCTAssertThrowsError(try RelayBatch.decode(message,identity:identity))
        message=batch.message;message["digest"]="wrong"
        XCTAssertThrowsError(try RelayBatch.decode(message,identity:identity))
        XCTAssertThrowsError(try RelayBatch(identity:identity,payload:Data(repeating:0,count:32769),count:1))
        XCTAssertThrowsError(try RelayBatch(identity:identity,payload:Data([1]),count:33))
    }
    func testDurableRelayAckLossReplayAndOriginBinding() async throws {
        let root=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at:root,withIntermediateDirectories:true);defer { try? FileManager.default.removeItem(at:root) }
        let source=try PhoneStore(path:root.appendingPathComponent("watch.journal"),device:"watch")
        let identity=try WatchIdentity(device:"watch",phone:"phone"),relayURL=root.appendingPathComponent("relay.journal")
        let inbox=RelayInbox(path:relayURL,identity:identity)
        let capture=try await source.startCapture(rate:16000) { _ in }
        let pcm=[Float](repeating:0.25,count:1600)
        for _ in 0..<8 { pcm.withUnsafeBufferPointer { XCTAssertTrue(capture.push(left:$0.baseAddress!,right:nil,frames:$0.count)) } }
        let stats=try await source.finishCapture();XCTAssertEqual(stats?.committed,8)
        do { _ = try await WatchTransfer.push(store:source,identity:identity) { message in _ = try await inbox.receive(message);throw PhoneError.invalid("Injected reply loss") };XCTFail("Expected lost ACK") } catch { }
        var acknowledged=try await source.acknowledgedCount(peer:"phone");XCTAssertEqual(acknowledged,0)
        let before=try Data(contentsOf:relayURL)
        do { _ = try await WatchTransfer.push(store:source,identity:identity) { message in
            let batch=try RelayBatch.decode(message,identity:identity);var ack=batch.ack(inserted:0);ack["digest"]="wrong";return ack
        };XCTFail("Expected malformed ACK") } catch { }
        acknowledged=try await source.acknowledgedCount(peer:"phone");XCTAssertEqual(acknowledged,0)
        let report=try await WatchTransfer.push(store:source,identity:identity) { try await inbox.receive($0) }
        XCTAssertEqual(report.delivered,0);XCTAssertEqual(report.duplicates,8)
        acknowledged=try await source.acknowledgedCount(peer:"phone");XCTAssertEqual(acknowledged,8)
        XCTAssertEqual(try Data(contentsOf:relayURL),before)
        try await source.beginSync();let (payload,count)=try await source.batch(offset:0,limit:32);await source.endSync()
        let valid=try RelayBatch(identity:identity,payload:payload,count:count);var mismatched=valid.message;mismatched["count"]=1
        do { _ = try await inbox.receive(mismatched);XCTFail("Expected count mismatch") } catch { }
        XCTAssertEqual(try Data(contentsOf:relayURL),before)
        let other=try PhoneStore(path:root.appendingPathComponent("other.journal"),device:"other")
        let otherCapture=try await other.startCapture(rate:16000) { _ in }
        pcm.withUnsafeBufferPointer { _ = otherCapture.push(left:$0.baseAddress!,right:nil,frames:$0.count) };_ = try await other.finishCapture()
        try await other.beginSync();let (forged,n)=try await other.batch(offset:0,limit:32);await other.endSync()
        let spoof=try RelayBatch(identity:identity,payload:forged,count:n)
        do { _ = try await inbox.receive(spoof.message);XCTFail("Expected origin rejection") } catch { }
        XCTAssertEqual(try Data(contentsOf:relayURL),before)
        try await other.close();try await source.close()
        let restored=try PhoneStore(path:root.appendingPathComponent("watch.journal"),device:"watch")
        acknowledged=try await restored.acknowledgedCount(peer:"phone");XCTAssertEqual(acknowledged,8);try await restored.close()
    }
}
