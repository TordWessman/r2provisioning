import Testing
@testable import r2provisioning

@Test func versionIsSet() {
    #expect(!R2Provisioning.version.isEmpty)
}
