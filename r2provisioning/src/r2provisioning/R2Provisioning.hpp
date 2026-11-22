#ifndef R2_PROVISIONING_HPP
#define R2_PROVISIONING_HPP

// r2provisioning — device provisioning for ESP32/ESP8266.
// Scaffold: the public API is a placeholder and will change.

namespace r2provisioning {

/// Library version string.
const char *version();

/// Entry point for device provisioning. Not yet implemented.
class R2Provisioning {
public:
    R2Provisioning() = default;

    /// Prepare the provisioning service. Returns true on success.
    bool begin();

    /// Drive the provisioning state machine; call from `loop()`.
    void loop();

    /// True once the device has been provisioned.
    bool isProvisioned() const { return provisioned_; }

private:
    bool provisioned_ = false;
};

} // namespace r2provisioning

#endif // R2_PROVISIONING_HPP
