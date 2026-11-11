#if os(iOS) || os(watchOS)
import Foundation
import WatchConnectivity

private final class ReplyGate {
    private let lock=NSLock()
    private var continuation:CheckedContinuation<[String:Any],Error>?
    private var finished=false
    private var timer:DispatchSourceTimer?
    func complete(_ result:Result<[String:Any],Error>) {
        lock.lock();guard !finished else { lock.unlock();return };finished=true
        let current=continuation;continuation=nil;timer?.cancel();timer=nil;lock.unlock();current?.resume(with:result)
    }
    func install(_ continuation:CheckedContinuation<[String:Any],Error>) -> Bool {
        lock.lock();defer { lock.unlock() }
        if finished { continuation.resume(throwing:CancellationError());return false }
        self.continuation=continuation
        let timer=DispatchSource.makeTimerSource();timer.schedule(deadline:.now()+10)
        timer.setEventHandler { [weak self] in self?.complete(.failure(PhoneError.invalid("Watch reply timed out; events retained"))) };self.timer=timer;timer.resume();return true
    }
}
public final class WatchLink:NSObject,WCSessionDelegate {
    public var onMessage:(([String:Any],@escaping ([String:Any])->Void)->Void)?
    public var onState:((Bool,Bool,String)->Void)?
    public var reachable:Bool { WCSession.isSupported() && WCSession.default.activationState == .activated && WCSession.default.isReachable }
    public func activate() { guard WCSession.isSupported() else { onState?(false,false,"WatchConnectivity unsupported");return };let session=WCSession.default;session.delegate=self;session.activate() }
    public func request(_ message:[String:Any]) async throws -> [String:Any] {
        guard reachable else { throw PhoneError.invalid("Keep both companion apps active and in range") }
        let gate=ReplyGate()
        return try await withTaskCancellationHandler(operation: {
            try Task.checkCancellation()
            return try await withCheckedThrowingContinuation { continuation in
                guard gate.install(continuation) else { return }
                WCSession.default.sendMessage(message,replyHandler:{ gate.complete(.success($0)) },errorHandler:{ gate.complete(.failure($0)) })
            }
        },onCancel:{gate.complete(.failure(CancellationError()))})
    }
    public func session(_ session:WCSession,activationDidCompleteWith state:WCSessionActivationState,error:Error?) {
        DispatchQueue.main.async { self.onState?(state == .activated,session.isReachable,error?.localizedDescription ?? "") }
    }
    public func sessionReachabilityDidChange(_ session:WCSession) { DispatchQueue.main.async { self.onState?(session.activationState == .activated,session.isReachable,"") } }
    public func session(_ session:WCSession,didReceiveMessage message:[String:Any],replyHandler:@escaping ([String:Any])->Void) {
        DispatchQueue.main.async { guard session.activationState == .activated,let handler=self.onMessage else { replyHandler(["kind":"ERROR","message":"Companion not enabled"]);return };handler(message,replyHandler) }
    }
#if os(iOS)
    public func sessionDidBecomeInactive(_ session:WCSession) { DispatchQueue.main.async { self.onState?(false,false,"Companion inactive") } }
    public func sessionDidDeactivate(_ session:WCSession) { DispatchQueue.main.async { self.onState?(false,false,"Watch changed; enable companion explicitly again") } }
#endif
}
#endif
