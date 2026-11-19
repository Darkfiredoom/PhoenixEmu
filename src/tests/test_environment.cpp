// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32
#include <cstdlib>

namespace {
// Tests write guest memory without the kernel committing it first, and Catch2's crash handler
// sees the fault before the reserved section's handler can commit the page: commit the whole
// section up front unless EDEN_NX2_SEC_RESERVE is set.
const bool sec_reserve_off = std::getenv("EDEN_NX2_SEC_RESERVE") != nullptr ||
                             _putenv_s("EDEN_NX2_SEC_RESERVE", "0") == 0;
} // namespace
#endif
