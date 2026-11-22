#include "R2Provisioning.hpp"

#include "R2Logger.h"

namespace r2provisioning {

const char *version() { return "0.0.1"; }

bool R2Provisioning::begin() {
    r2common::R2Logger::m("r2provisioning: begin (scaffold) v", version());
    return true;
}

void R2Provisioning::loop() {
    // TODO: provisioning state machine.
}

} // namespace r2provisioning
