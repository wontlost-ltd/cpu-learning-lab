import Foundation
import PhoneCore
@main struct Main {
    static func main() async {
        do {
            let args=CommandLine.arguments
            guard args.count==5,let port=UInt16(args[4]) else { throw PhoneError.invalid("phone-sync STORE PAIR IPv4 PORT") }
            let pair=try Pairing(data:Data(contentsOf:URL(fileURLWithPath:args[2])))
            let store=try PhoneStore(path:URL(fileURLWithPath:args[1]),device:pair.device)
            let result=try await PhoneSync.push(store:store,pair:pair,host:args[3],port:port)
            print("delivered=\(result.delivered) duplicates=\(result.duplicates) complete=1")
        } catch { fputs("phone sync failed: \(error.localizedDescription)\n",stderr);exit(1) }
    }
}
