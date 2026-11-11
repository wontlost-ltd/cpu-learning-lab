import SwiftUI
import AVFoundation
import PhoneCore

@MainActor final class WatchModel:ObservableObject {
    @Published var identity:WatchIdentity?
    @Published var offered:WatchIdentity?
    @Published var enabled=false
    @Published var reachable=false
    @Published var busy=false
    @Published var recording=false
    @Published var events=0
    @Published var confirmed=0
    @Published var dropped:UInt64=0
    @Published var status="先连接 iPhone，并在本机确认身份。默认不录音。"
    private let link=WatchLink()
    private var store:PhoneStore?
    private var engine:AVAudioEngine?
    private var tap=false,stopWanted=false,stopping=false
    private var isForeground=true
    private var transfer:Task<Void,Never>?
    private var path:URL { FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("watch.journal") }
    init() {
        link.onState={ [weak self] active,reachable,error in self?.enabled=active;self?.reachable=reachable;if !error.isEmpty { self?.status=error } }
        link.onMessage={ [weak self] message,reply in
            guard let self=self,self.isForeground,!self.busy,!self.recording else { reply(["kind":"ERROR","message":"Watch busy or inactive"]);return }
            do { let identity=try WatchIdentity.fromOffer(message)
                if let current=self.identity,current != identity { throw PhoneError.invalid("现有身份与日志不变，拒绝替换。") }
                self.offered=identity;self.status="请核对设备与 iPhone 身份后确认。"
                reply(["kind":"CPUAI_WATCH_OFFERED1","device":identity.device,"phone":identity.phone])
            } catch { reply(["kind":"ERROR","message":"Invalid or conflicting configuration"]);self.status=error.localizedDescription }
        }
    }
    private func open(_ identity:WatchIdentity) async throws {
        try FileManager.default.createDirectory(at:path.deletingLastPathComponent(),withIntermediateDirectories:true)
        if let store=store,store.device != identity.device { throw PhoneError.invalid("现有设备身份不可改写。") }
        if store == nil { store=try await PhoneStore.open(path:path,device:identity.device) }
        events=try await store!.count();confirmed=try await store!.acknowledgedCount(peer:identity.phone)
    }
    func restore() async {
        busy=true;defer { busy=false }
        do { if let identity=try WatchIdentity.load() { try await open(identity);self.identity=identity;status="身份与经验已恢复。连接、采集和补传需手动启动。" } }
        catch { status=error.localizedDescription }
    }
    func enable() { link.activate() }
    func accept() async {
        guard let offered=offered,!busy,!recording else { return };busy=true;defer { busy=false }
        do { try await open(offered);try offered.save();identity=offered;self.offered=nil;status="身份已确认，尚未采集。" } catch { status=error.localizedDescription }
    }
    func start() async {
        guard let store=store,identity != nil,!busy,!recording else { return };busy=true;stopWanted=false
        do {
            let granted=await withCheckedContinuation { c in AVAudioApplication.requestRecordPermission { c.resume(returning:$0) } }
            guard granted,!stopWanted else { throw PhoneError.invalid("麦克风未授权或采集已取消。") }
            let session=AVAudioSession.sharedInstance();try session.setCategory(.record,mode:.measurement,options:[]);try session.setPreferredSampleRate(16000);try session.setActive(true)
            let engine=AVAudioEngine();self.engine=engine;let input=engine.inputNode,format=input.outputFormat(forBus:0),rate=format.sampleRate
            guard format.commonFormat == .pcmFormatFloat32,!format.isInterleaved,(1...2).contains(format.channelCount),rate>=8000,rate<=48000,rate.rounded()==rate else { throw PhoneError.invalid("当前音频格式不支持。") }
            let handle=try await store.startCapture(rate:UInt32(rate)) { [weak self] result in
                DispatchQueue.main.async { guard let self=self else { return };switch result {
                case .success(let stats):self.events=stats.events;self.dropped=stats.dropped
                case .failure(let error):self.status=error.localizedDescription;Task { await self.stop(preserve:true) }
                } }
            }
            guard !stopWanted else { throw CancellationError() }
            input.installTap(onBus:0,bufferSize:1024,format:format) { buffer,_ in
                guard let channels=buffer.floatChannelData else { handle.discard(frames:Int(buffer.frameLength));return }
                handle.push(left:channels[0],right:buffer.format.channelCount==2 ? UnsafePointer(channels[1]):nil,frames:Int(buffer.frameLength))
            }
            tap=true;engine.prepare();try engine.start();recording=true;busy=false;status="前台采集特征；不保存原始音频。"
        } catch { status=error.localizedDescription;await stop(preserve:true) }
    }
    func stop(preserve:Bool=false) async {
        stopWanted=true;guard !stopping else { return };stopping=true;busy=true;defer { stopping=false;busy=false }
        engine?.stop();if tap { engine?.inputNode.removeTap(onBus:0);tap=false };engine=nil;recording=false
        do { if let stats=try await store?.finishCapture() { events=stats.events;dropped=stats.dropped;if !preserve { status="已停止；尾部丢弃 \(stats.discardedTail) 帧。" } } }
        catch { status=error.localizedDescription }
        try? AVAudioSession.sharedInstance().setActive(false)
    }
    func background() { isForeground=false;stopWanted=true;transfer?.cancel();if recording { Task { await stop() } } }
    func active() { isForeground=true }
    func interrupted() { stopWanted=true;if recording { Task { await stop() } } }
    func send() {
        guard let store=store,let identity=identity,!busy,!recording,enabled,reachable,isForeground else { return };busy=true
        transfer=Task {
            do { let result=try await WatchTransfer.push(store:store,identity:identity) { [link] message in try await link.request(message) }
                confirmed=try await store.acknowledgedCount(peer:identity.phone);status="iPhone 已保存：新增 \(result.delivered)，重复 \(result.duplicates)。不代表 Mac 已确认。"
            } catch { status="补传未完成，原件保留。\(error.localizedDescription)" }
            busy=false;transfer=nil
        }
    }
}
struct WatchView:View {
    @StateObject private var model=WatchModel()
    @Environment(\.scenePhase) private var scene
    var body:some View {
        ScrollView {
            VStack(alignment:.leading,spacing:10) {
                Text("CPU Learning Watch").font(.headline)
                Button("连接 iPhone") { model.enable() }.disabled(model.enabled || model.busy)
                Text(model.reachable ? "iPhone 可达":"iPhone 不可达")
                if let offered=model.offered {
                    Text("设备：\(offered.device)\niPhone：\(offered.phone)")
                    Button("确认此身份") { Task { await model.accept() } }.disabled(model.busy || model.recording)
                    Button("拒绝配置") { model.offered=nil }
                }
                if let identity=model.identity { Text(identity.device).font(.caption) }
                Button(model.recording ? "停止采集":"开始采集") { Task { if model.recording { await model.stop() } else { await model.start() } } }.disabled(model.identity == nil || model.busy || (!model.recording && model.events>=4096))
                Text("本机保留 \(model.events)/4096\niPhone 曾确认 \(model.confirmed)\n溢出帧 \(model.dropped)").font(.caption)
                Button("补传到 iPhone") { model.send() }.disabled(model.identity == nil || model.busy || model.recording || !model.enabled || !model.reachable)
                Text(model.status).font(.caption)
            }.padding()
        }
        .task { await model.restore() }
        .onChange(of:scene) { _,phase in if phase == .active { model.active() } else if phase == .background || model.recording { model.background() } }
        .onReceive(NotificationCenter.default.publisher(for:AVAudioSession.interruptionNotification)) { _ in model.interrupted() }
        .onReceive(NotificationCenter.default.publisher(for:AVAudioSession.routeChangeNotification)) { _ in if model.recording { model.interrupted() } }
    }
}
@main struct CPULearningWatchApp:App { var body:some Scene { WindowGroup { WatchView() } } }
