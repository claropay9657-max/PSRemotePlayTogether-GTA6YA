// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MACMOUSE_H
#define CHIAKI_MACMOUSE_H

#include <functional>

/**
 * The Mac's pointer held in place while a game has the mouse: it stays where it is put, is
 * free to "move" without end in any direction, and how far the mouse went arrives here
 * instead. This is how games take the mouse on a Mac; putting the pointer back by hand, as
 * the window does elsewhere, needs the Accessibility permission there.
 */
namespace macmouse
{

/**
 * Puts the pointer at this place on the screen (in points, from the main screen's top left)
 * and holds it there. `moved` is told on the main thread how far the mouse moved, in points
 * as the system counts them, to the right and down.
 */
void Begin(double screen_x, double screen_y, std::function<void(double dx, double dy)> moved);
/** Lets the pointer go again. */
void End();

} // namespace macmouse

#endif // CHIAKI_MACMOUSE_H
