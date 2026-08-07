/* stub_render.c - headless replacements for the raylib rendering layer.
 * Lets the simulation modules be tested without opening a window. */
#include "nl_core.h"

void nl_render_init(void) {}
void nl_render_shutdown(void) {}
void nl_render_frame(NLGame *g) { (void)g; }
void nl_render_rain_update(NLGame *g, float dt) { (void)g; (void)dt; }
