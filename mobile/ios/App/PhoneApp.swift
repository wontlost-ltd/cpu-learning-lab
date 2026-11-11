import SwiftUI
import AVFoundation
import UniformTypeIdentifiers
import PhoneCore

@MainActor final class PhoneModel: ObservableObject {
    @Published var pairing: Pairing?
    @Published var status="导入配对文件后，点击开始采集。默认不录音、不联网。"
    @Published var busy=false
    @Published var recording=false
    @Published var events=0
    @Published var dropped: UInt64=0
    @Published var host=""
    @Published var port="7443"
    private var store:PhoneStore?
    private var engine:AVAudioEngine?
    private var tapInstalled=false
    private var stopWanted=false
    private var stopping=false
    private var captureError:String?
    private var syncTask:Task<Void,Never>?
    private var journalURL:URL {
        let folder=FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0]
        return folder.appendingPathComponent("device.journal")
    }
    func restore() async {
        guard !busy,store == nil else { return };busy=true;defer { busy=false }
        do {
            try FileManager.default.createDirectory(at:journalURL.deletingLastPathComponent(),withIntermediateDirectories:true,attributes:[.protectionKey:FileProtectionType.complete])
            if let pair=try PairingKeychain.load() { try await install(pair);status="配对已恢复。采集和同步需要你主动启动。" }
        } catch { status=error.localizedDescription }
    }
    private func install(_ pair:Pairing) async throws {
        if let existing=store,existing.device != pair.device { throw PhoneError.invalid("新配对的设备 ID 与现有日志不同；已保留原数据。") }
        if store == nil { store=try await PhoneStore.open(path:journalURL,device:pair.device) }
        try FileManager.default.setAttributes([.protectionKey:FileProtectionType.complete],ofItemAtPath:journalURL.path)
        try PairingKeychain.save(pair);pairing=pair;events=try await store?.count() ?? 0
    }
    func accept(_ pair:Pairing) async {
        guard !busy,!recording else { return };busy=true;defer { busy=false }
        do { try await install(pair);status="配对已保存到 Keychain。设备：\(pair.device)" } catch { status=error.localizedDescription }
    }
    func start() async {
        guard let store=store,!busy,!recording else { return };busy=true;stopWanted=false;captureError=nil
        do {
            let granted=await withCheckedContinuation { continuation in AVAudioApplication.requestRecordPermission { continuation.resume(returning:$0) } }
            guard granted else { throw PhoneError.invalid("麦克风未授权。可在系统设置中修改。") }
            guard !stopWanted else { throw CancellationError() }
            let session=AVAudioSession.sharedInstance();try session.setCategory(.record,mode:.measurement,options:[])
            try session.setPreferredSampleRate(16000);try session.setActive(true)
            let engine=AVAudioEngine();self.engine=engine;let input=engine.inputNode,format=input.outputFormat(forBus:0)
            let rate=format.sampleRate
            guard format.commonFormat == .pcmFormatFloat32,!format.isInterleaved,(1...2).contains(format.channelCount),rate>=8000,rate<=48000,rate.rounded()==rate else { throw PhoneError.invalid("当前输入格式不支持：需要 8–48 kHz、单／双声道 Float32。") }
            let handle=try await store.startCapture(rate:UInt32(rate)) { [weak self] result in
                DispatchQueue.main.async {
                    guard let self=self else { return }
                    switch result {
                    case .success(let stats):self.events=stats.events;self.dropped=stats.dropped
                    case .failure(let error):self.captureError=error.localizedDescription;self.status=error.localizedDescription;Task { await self.stop(preserveStatus:true) }
                    }
                }
            }
            guard !stopWanted else { throw CancellationError() }
            input.installTap(onBus:0,bufferSize:1024,format:format) { buffer,_ in
                guard let channels=buffer.floatChannelData else { handle.discard(frames:Int(buffer.frameLength));return }
                handle.push(left:channels[0],right:buffer.format.channelCount==2 ? UnsafePointer(channels[1]):nil,frames:Int(buffer.frameLength))
            }
            tapInstalled=true;engine.prepare();try engine.start();recording=true;busy=false
            status="正在采集 100 ms 特征窗口（\(Int(rate)) Hz）。不保存原始音频，不自动教学。"
        } catch {
            status=error is CancellationError ? "采集已取消。":error.localizedDescription
            await stop(preserveStatus:true)
        }
    }
    func stop(preserveStatus:Bool=false) async {
        stopWanted=true
        guard !stopping else { return };stopping=true;busy=true
        defer { stopping=false;busy=false }
        engine?.stop();if tapInstalled { engine?.inputNode.removeTap(onBus:0);tapInstalled=false };engine=nil;recording=false
        do { if let stats=try await store?.finishCapture() { events=stats.events;dropped=stats.dropped;if !preserveStatus && captureError == nil { status="采集已停止。尾部丢弃 \(stats.discardedTail) 帧；溢出 \(stats.dropped) 帧。" } } }
        catch { status=error.localizedDescription }
        try? AVAudioSession.sharedInstance().setActive(false);busy=false
    }
    func background() {
        stopWanted=true;syncTask?.cancel()
        if recording { Task { await stop() } }
    }
    func interrupted() { stopWanted=true;if recording { Task { await stop() } } }
    func synchronize() {
        guard let store=store,let pair=pairing,!recording,!busy,let port=UInt16(port),port>0 else { return }
        busy=true;status="正在连接配对接收端并补传本设备事件…"
        syncTask=Task {
            do { let result=try await PhoneSync.push(store:store,pair:pair,host:host,port:port);status="同步完成：新增 \(result.delivered)，重复 \(result.duplicates)。" }
            catch { status="同步未完成，已保存事件可重试。\(error.localizedDescription)" }
            busy=false;syncTask=nil
        }
    }
}

struct PhoneView:View {
    @StateObject private var model=PhoneModel()
    @StateObject private var watch=PhoneWatchRelay()
    @State private var importingWatch=false
    @State private var pendingIsWatch=false
    @Environment(\.scenePhase) private var scene
    @State private var importing=false
    @State private var pending:Pairing?
    @State private var confirming=false
    var body:some View {
        NavigationStack {
            Form {
                Section("配对") {
                    if let pair=model.pairing { Text("设备：\(pair.device)");Text("接收端：\(pair.server)") }
                    Button("导入配对文件") { importingWatch=false;importing=true }.accessibilityIdentifier("importPairing").disabled(model.busy || model.recording)
                    Text("配对包由你信任的 Mac 接收端生成，包含敏感设备密钥。")
                }
                Section("前台采集") {
                    Button(model.recording ? "停止采集":"开始采集") {
                        Task { if model.recording { await model.stop() } else { await model.start() } }
                    }.accessibilityIdentifier("captureButton").disabled(model.busy || model.pairing == nil || (!model.recording && model.events>=4096))
                    Text("已持久化事件：\(model.events) / 4096");Text("本次溢出帧：\(model.dropped)")
                    Text("离开前台或音频中断时停止；不会自动恢复录音。")
                }
                Section("TLS 补传") {
                    TextField("Mac 局域网 IPv4",text:$model.host).keyboardType(.decimalPad).autocorrectionDisabled().textInputAutocapitalization(.never)
                    TextField("端口",text:$model.port).keyboardType(.numberPad)
                    Button("同步到 Mac") { model.synchronize() }.accessibilityIdentifier("syncButton").disabled(model.busy || model.recording || model.pairing == nil || model.host.isEmpty || UInt16(model.port) == nil || UInt16(model.port) == 0)
                    Text("采集结束后手动同步。仅发送特征；不触发模型学会事件。")
                }
                Section("Apple Watch 中继") {
                    if let pair=watch.watchPair { Text("手表设备：\(pair.device)") }
                    Button("导入手表专用配对包") { importingWatch=true;importing=true }.disabled(model.pairing == nil || model.busy || model.recording || watch.busy)
                    Button("启用手表连接") { watch.enable() }.disabled(watch.busy || watch.enabled)
                    Text(watch.reachable ? "手表当前可达":"手表当前不可达")
                    Button("向手表发送身份配置") { Task { await watch.configureWatch() } }.disabled(watch.watchPair == nil || !watch.enabled || !watch.reachable || watch.busy)
                    Text("iPhone 保留的手表事件：\(watch.events)")
                    Button("补传手表事件到 Mac") { if let port=UInt16(model.port) { watch.forward(host:model.host,port:port) } }.disabled(watch.watchPair == nil || watch.busy || model.busy || model.recording || model.host.isEmpty || UInt16(model.port) == nil || UInt16(model.port) == 0)
                    Text(watch.status)
                    Text("只确认当前系统配对的手表。收到 iPhone 回执不代表 Mac 已收到。")
                }
                Section("状态") { Text(model.status).accessibilityIdentifier("statusText") }
            }.navigationTitle("CPU Learning Phone")
        }
        .task { await model.restore();await watch.restore(phone:model.pairing) }
        .onChange(of:model.pairing) { _,pair in watch.phoneChanged(pair) }
        .onChange(of:scene) { _,phase in watch.foreground(phase == .active);if phase == .background || (phase == .inactive && model.recording) { model.background() } }
        .onReceive(NotificationCenter.default.publisher(for:AVAudioSession.interruptionNotification)) { _ in model.interrupted() }
        .onReceive(NotificationCenter.default.publisher(for:AVAudioSession.routeChangeNotification)) { _ in if model.recording { model.interrupted() } }
        .fileImporter(isPresented:$importing,allowedContentTypes:[.data,.plainText]) { result in
            do {
                let url=try result.get();let scoped=url.startAccessingSecurityScopedResource();defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                let handle=try FileHandle(forReadingFrom:url);defer { try? handle.close() }
                let data=try handle.read(upToCount:1025) ?? Data();pending=try Pairing(data:data);pendingIsWatch=importingWatch;confirming=true
            } catch { model.status=error.localizedDescription }
        }
        .alert("确认信任此配对包？",isPresented:$confirming,presenting:pending) { pair in
            Button("导入并信任") { Task { if pendingIsWatch { await watch.accept(pair,phone:model.pairing) } else { await model.accept(pair) } };pending=nil }
            Button("取消",role:.cancel) { pending=nil }
        } message: { pair in Text("接收端：\(pair.server)\n设备：\(pair.device)\nSHA256：\(pair.pin)\n请确认文件来自你信任的接收端。") }
    }
}
@main struct CPULearningPhoneApp:App { var body:some Scene { WindowGroup { PhoneView() } } }
