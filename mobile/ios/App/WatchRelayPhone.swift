import Foundation
import PhoneCore

@MainActor final class PhoneWatchRelay:ObservableObject {
    @Published var watchPair:Pairing?
    @Published var enabled=false
    @Published var reachable=false
    @Published var busy=false
    @Published var events=0
    @Published var status="手表连接默认关闭。先导入专用手表配对包。"
    private let link=WatchLink()
    private var phonePair:Pairing?
    private var isForeground=true
    private var syncTask:Task<Void,Never>?
    private var path:URL { FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("watch-relay.journal") }
    init() {
        link.onState={ [weak self] active,reachable,error in self?.enabled=active;self?.reachable=reachable;if !error.isEmpty { self?.status=error } }
        link.onMessage={ [weak self] message,reply in
            Task { guard let self=self else { reply(["kind":"ERROR","message":"Receiver unavailable"]);return };await self.receive(message,reply:reply) }
        }
    }
    private func identity() throws -> WatchIdentity {
        guard let phone=phonePair,let watch=watchPair,phone.server==watch.server,phone.pin==watch.pin,phone.device != watch.device else { throw PhoneError.invalid("请导入与 iPhone 接收端相同、设备 ID 不同的手表配对包。") }
        return try WatchIdentity(device:watch.device,phone:phone.device)
    }
    func restore(phone:Pairing?) async {
        phonePair=phone
        do { watchPair=try PairingKeychain.load(account:"watch-pair");if watchPair != nil { _ = try identity();let store=try await PhoneStore.open(path:path,device:watchPair!.device);events=try await store.count();try await store.close() } }
        catch { status=error.localizedDescription }
    }
    func phoneChanged(_ pair:Pairing?) { phonePair=pair;if watchPair != nil { do { _ = try identity() } catch { status=error.localizedDescription } } }
    func foreground(_ value:Bool) { isForeground=value;if !value { syncTask?.cancel() } }
    func enable() { guard !busy else { return };link.activate();status="连接请求已启用；保持手机和手表 App 在前台。" }
    func accept(_ pair:Pairing,phone:Pairing?) async {
        guard !busy else { return };busy=true;defer { busy=false };phonePair=phone
        do {
            guard let phone=phone,phone.server==pair.server,phone.pin==pair.pin,pair.device != phone.device else { throw PhoneError.invalid("手表凭据必须属于同一接收端的独立设备 ID。") }
            if let old=watchPair,old.device != pair.device { throw PhoneError.invalid("已保留原手表日志；不支持静默切换手表身份。") }
            let store=try await PhoneStore.open(path:path,device:pair.device);events=try await store.count();try await store.close()
            try FileManager.default.setAttributes([.protectionKey:FileProtectionType.complete],ofItemAtPath:path.path)
            try PairingKeychain.save(pair,account:"watch-pair");watchPair=pair;status="手表授权已保存在 iPhone Keychain，不会把服务密钥传给手表。"
        } catch { status=error.localizedDescription }
    }
    func configureWatch() async {
        guard !busy,isForeground,enabled,reachable else { return };busy=true;defer { busy=false }
        do { let identity=try identity();let reply=try await link.request(identity.offer)
            guard reply["kind"] as? String=="CPUAI_WATCH_OFFERED1",reply["device"] as? String==identity.device,reply["phone"] as? String==identity.phone else { throw PhoneError.invalid("配置未被接受；原身份与数据保留。") }
            status="配置已送达，请在手表上核对身份并确认。"
        } catch { status=error.localizedDescription }
    }
    private func receive(_ message:[String:Any],reply:@escaping ([String:Any])->Void) async {
        guard isForeground,enabled,!busy else { reply(["kind":"ERROR","message":"Keep iPhone app active; receiver busy or disabled"]);return }
        busy=true;defer { busy=false }
        do {
            let identity=try identity();let inbox=RelayInbox(path:path,identity:identity);let ack=try await inbox.receive(message)
            let store=try await PhoneStore.open(path:path,device:identity.device);events=try await store.count();try await store.close()
            status="手表批次已持久化到 iPhone；尚不代表 Mac 已确认。";reply(ack)
        } catch { status=error.localizedDescription;reply(["kind":"ERROR","message":"Relay not committed; retained source may retry"] ) }
    }
    func forward(host:String,port:UInt16) {
        guard !busy,isForeground,let pair=watchPair else { return }
        do { _ = try identity() } catch { status=error.localizedDescription;return }
        busy=true
        syncTask=Task {
            var store:PhoneStore?
            do { let opened=try await PhoneStore.open(path:path,device:pair.device);store=opened
                let result=try await PhoneSync.push(store:opened,pair:pair,host:host,port:port);try await opened.close();store=nil
                status="Mac 已确认手表补传：新增 \(result.delivered)，重复 \(result.duplicates)。"
            } catch { if let store=store { try? await store.close() };status="Mac 补传未完成，iPhone 中继与手表原件均保留。\(error.localizedDescription)" }
            busy=false;syncTask=nil
        }
    }
}
