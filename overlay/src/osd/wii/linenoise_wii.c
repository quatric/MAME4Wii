// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
/*
 * linenoise_wii.c - stand-in for linenoise on the Wii, which has no
 * terminal.  Interactive console input is simply unavailable.
 */

#include <stddef.h>
#include <stdlib.h>

#include "linenoise.h"

linenoiseCompletionCallback *linenoiseSetCompletionCallback(linenoiseCompletionCallback *comp, void *userdata) { return NULL; }
void linenoiseAddCompletion(linenoiseCompletions *comp, const char *str) { }
void linenoiseSetHintsCallback(linenoiseHintsCallback *callback, void *userdata) { }
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *callback) { }
char *linenoise(const char *prompt) { return NULL; }
char *linenoiseWithInitial(const char *prompt, const char *initial) { return NULL; }
void linenoiseClearScreen(void) { }
int linenoiseHistoryAdd(const char *line) { return 0; }
int linenoiseHistorySetMaxLen(int len) { return 0; }
int linenoiseHistoryGetMaxLen(void) { return 0; }
int linenoiseHistorySave(const char *filename) { return -1; }
int linenoiseHistoryLoad(const char *filename) { return -1; }
void linenoiseHistoryFree(void) { }
char **linenoiseHistory(int *len) { if (len) *len = 0; return NULL; }
int linenoiseColumns(void) { return 80; }
void linenoiseSetMultiLine(int enableml) { }
