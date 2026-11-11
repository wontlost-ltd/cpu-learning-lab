import Foundation
import CPUBridge

public struct CaptureStats {
    public let committed: UInt64
    public let dropped: UInt64
    public let discardedTail: UInt64
    public let discontinuities: UInt64
    public let events: Int
    init(_ s: CPLStats) { committed=s.committed_windows;dropped=s.dropped_frames;discardedTail=s.discarded_tail;discontinuities=s.discontinuities;events=Int(s.journal_events) }
}
// Retained by both the tap closure and worker until the tap has stopped.
public final class CaptureHandle {
    let pointer: OpaquePointer
    init(_ pointer: OpaquePointer) { self.pointer=pointer }
    deinit { cpl_capture_destroy(pointer) }
    public func discard(frames: Int) { _ = cpl_push_float(pointer,nil,nil,frames) }
    @discardableResult public func push(left: UnsafePointer<Float>,right: UnsafePointer<Float>?,frames: Int) -> Bool {
        cpl_push_float(pointer,left,right,frames)==1
    }
}
public final class PhoneStore {
    public let device: String
    private let worker=DispatchQueue(label:"cpu-learning.storage",qos:.utility)
    private var journal: OpaquePointer?
    private var capture: CaptureHandle?
    private var timer: DispatchSourceTimer?
    private var syncing=false
    public init(path: URL,device: String) throws {
        self.device=device
        var error=[CChar](repeating:0,count:512)
        guard let handle=cpl_open(path.path,device,&error,512) else { throw PhoneError.invalid(String(cString:error)) }
        journal=handle
    }
    public static func open(path: URL,device: String) async throws -> PhoneStore {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos:.utility).async { do { continuation.resume(returning:try PhoneStore(path:path,device:device)) } catch { continuation.resume(throwing:error) } }
        }
    }
    deinit { timer?.cancel();cpl_close(journal) }
    private func perform<T>(_ body: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in worker.async { do { continuation.resume(returning:try body()) } catch { continuation.resume(throwing:error) } } }
    }
    public func close() async throws {
        try await perform { guard self.capture == nil && !self.syncing else { throw PhoneError.invalid("Stop capture/sync before closing journal") };cpl_close(self.journal);self.journal=nil }
    }
    public func count() async throws -> Int { try await perform { Int(cpl_event_count(self.journal)) } }
    public func startCapture(rate: UInt32,onUpdate: @escaping (Result<CaptureStats,Error>)->Void) async throws -> CaptureHandle {
        try await perform {
            guard self.capture == nil && !self.syncing else { throw PhoneError.invalid("Stop capture/sync before starting another operation") }
            var error=[CChar](repeating:0,count:512)
            guard let pointer=cpl_capture_create(self.journal,rate,UUID().uuidString,&error,512) else { throw PhoneError.invalid(String(cString:error)) }
            let handle=CaptureHandle(pointer);self.capture=handle
            let timer=DispatchSource.makeTimerSource(queue:self.worker);timer.schedule(deadline:.now(),repeating:.milliseconds(20))
            timer.setEventHandler { [weak self] in
                guard let self=self else { return };var stats=CPLStats();var error=[CChar](repeating:0,count:512)
                if cpl_poll(handle.pointer,8,&stats,&error,512)==0 { onUpdate(.success(CaptureStats(stats))) }
                else { self.timer?.cancel();onUpdate(.failure(PhoneError.invalid(String(cString:error)))) }
            }
            self.timer=timer;timer.resume();return handle
        }
    }
    // Caller must first stop engine/remove tap, so producer is no longer running.
    public func finishCapture() async throws -> CaptureStats? {
        try await perform {
            guard let handle=self.capture else { return nil };self.timer?.cancel();self.timer=nil;self.capture=nil
            var stats=CPLStats();var error=[CChar](repeating:0,count:512)
            guard cpl_finish(handle.pointer,&stats,&error,512)==0 else { throw PhoneError.invalid(String(cString:error)) }
            return CaptureStats(stats)
        }
    }
    func beginSync() async throws {
        try await perform { guard self.capture == nil && !self.syncing else { throw PhoneError.invalid("Stop capture before syncing") };self.syncing=true }
    }
    func endSync() async { _ = try? await perform { self.syncing=false } }
    func batch(offset: Int) async throws -> (Data,Int) {
        try await perform {
            guard self.syncing else { throw PhoneError.invalid("Sync not active") }
            var bytes=[UInt8](repeating:0,count:131072),error=[CChar](repeating:0,count:512);var size=0,count=0
            guard cpl_batch(self.journal,offset,&bytes,131072,&size,&count,&error,512)==0 else { throw PhoneError.invalid(String(cString:error)) }
            return (Data(bytes.prefix(size)),count)
        }
    }
    func acknowledge(peer: String,payload: Data) async throws {
        try await perform {
            var error=[CChar](repeating:0,count:512)
            let result=payload.withUnsafeBytes { bytes in cpl_ack(self.journal,peer,bytes.bindMemory(to:UInt8.self).baseAddress,payload.count,&error,512) }
            guard result==0 else { throw PhoneError.invalid(String(cString:error)) }
        }
    }
}
