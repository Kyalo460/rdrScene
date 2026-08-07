/* main.c - Nairobi Life
 *
 * A realistic street-life survival simulation set in Nairobi, Kenya.
 * Entry point: owns the window and drives the fixed update / render loop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "raylib.h"
#include "nl_core.h"

/* The game state is large (buildings, props, NPCs, vehicles, rain).
 * Keep it out of the stack. */
static NLGame g_game;

int main(int argc, char **argv)
{
    uint32_t seed = (uint32_t)time(NULL);
    if (argc > 1) seed = (uint32_t)strtoul(argv[1], NULL, 10);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(NL_SCREEN_W, NL_SCREEN_H, "Nairobi Life");
    SetTargetFPS(NL_TARGET_FPS);
    SetExitKey(KEY_NULL);   /* Esc pauses; it must not close the window. */

    nl_render_init();
    nl_game_init(&g_game, seed);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        /* Clamp dt so that a stall (window drag, breakpoint) cannot make the
         * simulation jump a huge amount in a single step. */
        if (dt > 0.05f) dt = 0.05f;

        nl_game_input(&g_game, dt);
        nl_game_update(&g_game, dt);
        nl_render_frame(&g_game);
    }

    nl_render_shutdown();
    CloseWindow();
    return 0;
}
