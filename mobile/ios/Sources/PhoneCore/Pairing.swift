import Foundation
import Security

public enum PhoneError: Error, LocalizedError {
    case invalid(String)
    public var errorDescription: String? { if case let .invalid(message) = self { return message }; return "Operation failed" }
}
public struct Pairing: Equatable {
    public let server: String
    public let device: String
    public let pin: String
    let token: String
    public static func validID(_ value: String) -> Bool {
        !value.isEmpty && value.utf8.count <= 64 && value.utf8.allSatisfy { (65...90).contains($0) || (97...122).contains($0) || (48...57).contains($0) || [45,46,95].contains($0) }
    }
    public init(data: Data) throws {
        guard data.count <= 1024, let text = String(data: data, encoding: .utf8) else { throw PhoneError.invalid("Invalid pairing file") }
        let parts = text.split(whereSeparator: { $0.isWhitespace }).map(String.init)
        func hex(_ s: String) -> Bool { s.utf8.count == 64 && s.utf8.allSatisfy { (48...57).contains($0) || (97...102).contains($0) } }
        guard parts.count == 5, parts[0] == "CPUAI_PAIR1", Self.validID(parts[1]), Self.validID(parts[2]), parts[1] != parts[2], hex(parts[3]), hex(parts[4]) else { throw PhoneError.invalid("Invalid pairing identity or credentials") }
        server=parts[1];device=parts[2];pin=parts[3];token=parts[4]
    }
    public var encoded: Data { Data("CPUAI_PAIR1 \(server) \(device) \(pin) \(token)\n".utf8) }
}
enum SecureItem {
    private static func query(_ account:String)->[String:Any] { [kSecClass as String:kSecClassGenericPassword,kSecAttrService as String:"com.wontlost.cpu-learning-phone",kSecAttrAccount as String:account] }
    static func load(_ account:String) throws -> Data? {
        var q=query(account);q[kSecReturnData as String]=true;q[kSecMatchLimit as String]=kSecMatchLimitOne
        var value:CFTypeRef?;let status=SecItemCopyMatching(q as CFDictionary,&value)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess,let data=value as? Data else { throw PhoneError.invalid("Keychain unavailable; unlock device") };return data
    }
    static func save(_ data:Data,account:String) throws {
        let attributes:[String:Any]=[kSecValueData as String:data,kSecAttrAccessible as String:kSecAttrAccessibleWhenUnlockedThisDeviceOnly]
        let q=query(account);var status=SecItemUpdate(q as CFDictionary,attributes as CFDictionary)
        if status == errSecItemNotFound { var item=q;attributes.forEach { item[$0.key]=$0.value };status=SecItemAdd(item as CFDictionary,nil) }
        guard status == errSecSuccess else { throw PhoneError.invalid("Cannot save credential to Keychain") }
    }
}
public enum PairingKeychain {
    public static func load(account:String="device-pair") throws -> Pairing? { guard let data=try SecureItem.load(account) else { return nil };return try Pairing(data:data) }
    public static func save(_ pair:Pairing,account:String="device-pair") throws { try SecureItem.save(pair.encoded,account:account) }
}
