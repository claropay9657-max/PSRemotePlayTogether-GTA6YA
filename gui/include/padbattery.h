// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_PADBATTERY_H
#define PSRPT_PADBATTERY_H

namespace padbattery {

/**
 * The lowest charge, in percent, of the Xbox controllers connected over Bluetooth, as Windows'
 * own Bluetooth settings show it; -1 if none says (or not on Windows). SDL cannot tell it for
 * them: Windows' XInput gives no battery for a controller over Bluetooth. Asks Windows at
 * most every 10 seconds.
 */
#ifdef _WIN32
int XboxBluetooth();
#else
inline int XboxBluetooth() { return -1; }
#endif

} // namespace padbattery

#endif // PSRPT_PADBATTERY_H
