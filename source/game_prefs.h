#ifndef DARK_LANDS_GAME_PREFS_H
#define DARK_LANDS_GAME_PREFS_H

#ifdef __cplusplus
extern "C" {
#endif

void game_prefs_init(void);
void game_prefs_flush(const char* reason);
void game_prefs_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
