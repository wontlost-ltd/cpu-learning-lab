import Foundation
import CryptoKit

public struct WatchIdentity:Equatable {
    public let device:String
    public let phone:String
    public init(device:String,phone:String) throws {
        guard Pairing.validID(device),Pairing.validID(phone),device != phone else { throw PhoneError.invalid("Invalid watch/phone identity") }
        self.device=device;self.phone=phone
    }
    public init(data:Data) throws {
        guard data.count<=256,let text=String(data:data,encoding:.utf8) else { throw PhoneError.invalid("Invalid watch identity") }
        let fields=text.split(whereSeparator:{$0.isWhitespace}).map(String.init)
        guard fields.count==3,fields[0]=="CPUAI_WATCH1" else { throw PhoneError.invalid("Invalid watch identity") }
        try self.init(device:fields[1],phone:fields[2])
    }
    public static func load() throws -> WatchIdentity? { guard let data=try SecureItem.load("watch-identity") else { return nil };return try WatchIdentity(data:data) }
    public func save() throws { try SecureItem.save(Data("CPUAI_WATCH1 \(device) \(phone)\n".utf8),account:"watch-identity") }
    public var offer:[String:Any] { ["kind":"CPUAI_WATCH_OFFER1","device":device,"phone":phone] }
    public static func fromOffer(_ message:[String:Any]) throws -> WatchIdentity {
        guard message.count==3,message["kind"] as? String=="CPUAI_WATCH_OFFER1",let device=message["device"] as? String,let phone=message["phone"] as? String else { throw PhoneError.invalid("Invalid configuration offer") }
        return try WatchIdentity(device:device,phone:phone)
    }
}
public struct RelayBatch {
    public let identity:WatchIdentity
    public let payload:Data
    public let count:Int
    public let digest:String
    public init(identity:WatchIdentity,payload:Data,count:Int) throws {
        guard (1...32).contains(count),!payload.isEmpty,payload.count<=32768 else { throw PhoneError.invalid("Relay requires 1...32 events / 32 KiB") }
        self.identity=identity;self.payload=payload;self.count=count;digest=SHA256.hash(data:payload).map{String(format:"%02x",$0)}.joined()
    }
    public var message:[String:Any] { ["kind":"CPUAI_WATCH_BATCH1","device":identity.device,"phone":identity.phone,"count":count,"payload":payload,"digest":digest] }
    private static func integer(_ value:Any?) -> Int? {
        guard let number=value as? NSNumber,CFGetTypeID(number) != CFBooleanGetTypeID(),number.doubleValue==Double(number.intValue) else { return nil };return number.intValue
    }
    public static func decode(_ message:[String:Any],identity:WatchIdentity) throws -> RelayBatch {
        guard message.count==6,message["kind"] as? String=="CPUAI_WATCH_BATCH1",message["device"] as? String==identity.device,message["phone"] as? String==identity.phone,let payload=message["payload"] as? Data,let count=integer(message["count"]) else { throw PhoneError.invalid("Unconfigured relay origin or malformed message") }
        let batch=try RelayBatch(identity:identity,payload:payload,count:count)
        guard message["digest"] as? String==batch.digest else { throw PhoneError.invalid("Relay payload digest mismatch") };return batch
    }
    public func ack(inserted:Int)->[String:Any] { ["kind":"CPUAI_WATCH_ACK1","device":identity.device,"phone":identity.phone,"count":count,"digest":digest,"inserted":inserted] }
    public func verify(_ message:[String:Any]) throws -> Int {
        guard message.count==6,message["kind"] as? String=="CPUAI_WATCH_ACK1",message["device"] as? String==identity.device,message["phone"] as? String==identity.phone,Self.integer(message["count"])==count,message["digest"] as? String==digest,let inserted=Self.integer(message["inserted"]),(0...count).contains(inserted) else { throw PhoneError.invalid("Invalid durable relay acknowledgement") };return inserted
    }
}
public actor RelayInbox {
    private var receiving=false
    private let path:URL
    public let identity:WatchIdentity
    public init(path:URL,identity:WatchIdentity) { self.path=path;self.identity=identity }
    public func receive(_ message:[String:Any]) async throws -> [String:Any] {
        guard !receiving else { throw PhoneError.invalid("Relay receiver busy; retry retained events") };receiving=true;defer { receiving=false }
        let batch=try RelayBatch.decode(message,identity:identity)
        let store=try await PhoneStore.open(path:path,device:identity.device)
        do {
            let inserted=try await store.receiveRelay(payload:batch.payload,count:batch.count)
            try await store.close();return batch.ack(inserted:inserted)
        } catch { try? await store.close();throw error }
    }
}
public enum WatchTransfer {
    // Transport is injected for contract tests; production uses WCSession's live messaging.
    public static func push(store:PhoneStore,identity:WatchIdentity,send:@escaping ([String:Any]) async throws -> [String:Any]) async throws -> SyncResult {
        guard store.device==identity.device else { throw PhoneError.invalid("Watch journal identity mismatch") }
        try await store.beginSync();let clock=ContinuousClock();let deadline=clock.now.advanced(by:.seconds(60))
        do {
            var offset=0,inserted=0,duplicates=0
            for _ in 0..<128 {
                try Task.checkCancellation();guard clock.now<deadline else { throw PhoneError.invalid("Relay time budget exhausted; retry retained events") }
                let (payload,count)=try await store.batch(offset:offset,limit:32)
                if count==0 { await store.endSync();return SyncResult(delivered:inserted,duplicates:duplicates) }
                let batch=try RelayBatch(identity:identity,payload:payload,count:count)
                let reply=try await send(batch.message);let added=try batch.verify(reply)
                try await store.acknowledge(peer:identity.phone,payload:payload);offset+=count;inserted+=added;duplicates+=count-added
            }
            let (_,remaining)=try await store.batch(offset:offset,limit:32)
            guard remaining==0 else { throw PhoneError.invalid("Relay batch budget exhausted") }
            await store.endSync();return SyncResult(delivered:inserted,duplicates:duplicates)
        } catch { await store.endSync();throw error }
    }
}
