import Foundation
import Network
import Security
import CryptoKit

private final class FramedConnection {
    let connection: NWConnection
    let queue=DispatchQueue(label:"cpu-learning.tls")
    var timeout: DispatchSourceTimer?
    init(pair: Pairing,host: String,port: UInt16) throws {
        guard IPv4Address(host) != nil,port>0 else { throw PhoneError.invalid("Use receiver IPv4 address and port 1...65535") }
        let tls=NWProtocolTLS.Options()
        sec_protocol_options_set_min_tls_protocol_version(tls.securityProtocolOptions,.TLSv13)
        sec_protocol_options_set_verify_block(tls.securityProtocolOptions,{ _,trust,complete in
            let reference=sec_trust_copy_ref(trust).takeRetainedValue()
            guard let chain=SecTrustCopyCertificateChain(reference) as? [SecCertificate],let certificate=chain.first else { complete(false);return }
            let bytes=SecCertificateCopyData(certificate) as Data
            let actual=SHA256.hash(data:bytes).map { String(format:"%02x",$0) }.joined()
            guard actual==pair.pin else { complete(false);return }
            SecTrustSetAnchorCertificates(reference,[certificate] as CFArray)
            SecTrustSetAnchorCertificatesOnly(reference,true)
            SecTrustSetPolicies(reference,SecPolicyCreateBasicX509())
            SecTrustSetNetworkFetchAllowed(reference,false)
            complete(SecTrustEvaluateWithError(reference,nil))
        },queue)
        let parameters=NWParameters(tls:tls,tcp:NWProtocolTCP.Options())
        connection=NWConnection(host:NWEndpoint.Host(host),port:NWEndpoint.Port(rawValue:port)!,using:parameters)
    }
    func start() async throws {
        try await withCheckedThrowingContinuation { (continuation:CheckedContinuation<Void,Error>) in
            var resumed=false
            connection.stateUpdateHandler={ state in
                guard !resumed else { return }
                switch state {
                case .ready:resumed=true;continuation.resume()
                case .failed(let error):resumed=true;continuation.resume(throwing:error)
                case .cancelled:resumed=true;continuation.resume(throwing:PhoneError.invalid("Connection cancelled or timed out"))
                default:break
                }
            }
            let timer=DispatchSource.makeTimerSource(queue:queue);timer.schedule(deadline:.now()+10);timer.setEventHandler { [weak self] in self?.connection.cancel() };timeout=timer;timer.resume()
            connection.start(queue:queue)
        }
    }
    func close() { timeout?.cancel();connection.cancel() }
    func send(_ data: Data) async throws {
        guard !data.isEmpty,data.count<=131072 else { throw PhoneError.invalid("Invalid frame length") }
        let n=UInt32(data.count);var frame=Data([UInt8((n>>24)&255),UInt8((n>>16)&255),UInt8((n>>8)&255),UInt8(n&255)]);frame.append(data)
        try await withCheckedThrowingContinuation { (continuation:CheckedContinuation<Void,Error>) in
            connection.send(content:frame,completion:.contentProcessed { error in if let error=error { continuation.resume(throwing:error) } else { continuation.resume() } })
        }
    }
    private func exact(_ count: Int) async throws -> Data {
        var output=Data()
        while output.count<count {
            let remaining=count-output.count
            let part:Data=try await withCheckedThrowingContinuation { continuation in
                connection.receive(minimumIncompleteLength:remaining,maximumLength:remaining) { data,_,_,error in
                    if let error=error { continuation.resume(throwing:error) }
                    else if let data=data,!data.isEmpty { continuation.resume(returning:data) }
                    else { continuation.resume(throwing:PhoneError.invalid("Connection closed before complete frame")) }
                }
            }
            output.append(part)
        }
        return output
    }
    func receive(maximum: Int=2048) async throws -> String {
        let header=try await exact(4);let size=header.reduce(UInt32(0)) { ($0<<8)|UInt32($1) }
        guard size>0,size<=maximum else { throw PhoneError.invalid("Receiver frame exceeds limit") }
        guard let value=String(data:try await exact(Int(size)),encoding:.utf8) else { throw PhoneError.invalid("Invalid receiver text") };return value
    }
}
public struct SyncResult { public let delivered:Int;public let duplicates:Int }
public enum PhoneSync {
    public static func push(store: PhoneStore,pair: Pairing,host: String,port: UInt16) async throws -> SyncResult {
        guard store.device==pair.device else { throw PhoneError.invalid("Pairing does not match local device journal") }
        try await store.beginSync()
        do {
            let channel=try FramedConnection(pair:pair,host:host,port:port)
            defer { channel.close() }
            let result=try await withTaskCancellationHandler(operation: {
                try Task.checkCancellation();try await channel.start()
                try await channel.send(Data("HELLO CPUAI_NET1 \(pair.server) \(pair.device) \(pair.token)".utf8))
                guard try await channel.receive()=="READY CPUAI_NET1 \(pair.server)" else { throw PhoneError.invalid("Receiver identity mismatch") }
                var offset=0,delivered=0,duplicates=0
                while true {
                    try Task.checkCancellation();let (payload,count)=try await store.batch(offset:offset);if count==0 { break }
                    try await channel.send(payload)
                    let parts=try await channel.receive().split(whereSeparator: { $0.isWhitespace }).map(String.init)
                    let hash=SHA256.hash(data:payload).map { String(format:"%02x",$0) }.joined()
                    guard parts.count==5,parts[0]=="ACK",let total=Int(parts[1]),let inserted=Int(parts[2]),total==count,inserted>=0,inserted<=total,parts[3]==hash,parts[4]==pair.server else { throw PhoneError.invalid("Invalid commit acknowledgement") }
                    try await store.acknowledge(peer:pair.server,payload:payload);offset+=count;delivered+=inserted;duplicates+=count-inserted
                }
                try await channel.send(Data("END".utf8))
                guard try await channel.receive()=="DONE" else { throw PhoneError.invalid("Incomplete sync session") }
                return SyncResult(delivered:delivered,duplicates:duplicates)
            },onCancel:{ channel.connection.cancel() })
            await store.endSync();return result
        } catch { await store.endSync();throw error }
    }
}
