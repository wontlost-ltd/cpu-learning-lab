import Foundation
import PhoneCore
@main struct Main {
    static func main() async {
        do {
            let args=CommandLine.arguments;guard args.count==5 else { throw PhoneError.invalid("phone-relay SOURCE RELAY WATCH_ID PHONE_ID (injected local transport)") }
            let identity=try WatchIdentity(device:args[3],phone:args[4])
            let source=try PhoneStore(path:URL(fileURLWithPath:args[1]),device:identity.device)
            let inbox=RelayInbox(path:URL(fileURLWithPath:args[2]),identity:identity)
            let result=try await WatchTransfer.push(store:source,identity:identity) { try await inbox.receive($0) }
            try await source.close();print("relay inserted=\(result.delivered) duplicates=\(result.duplicates)")
        } catch { fputs("relay failed: \(error.localizedDescription)\n",stderr);exit(1) }
    }
}
