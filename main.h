#ifndef EDITOR_H
#define EDITOR_H

#include "termio.h"

#define CTRL_KEY(k) ((k) & 0x1f)
#define CUSTOM_EDITOR_VERSION "0.0.1"

enum editorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DELETE_KEY
};

typedef struct editorRow
{
    int size;
    char *chars;
} editorRow;

typedef struct editorConfig
{
    int cx, cy;
    struct termios orig_termios;
    int screenRows;
    int screenCols;
    int numRows;
    editorRow row;
} editorConfig;

typedef struct
{
    char *b;
    int len;
} aBuf;

void die(const char *s);

void disableRawMode();

void enableRawMode();

int editorReadKey();

void editorProcessKeypress();

void editorRefreshScreen();

void editorDrawRows(aBuf *ab);

int getWindowSize(int *rows, int *cols);

void initEditorConfig();

int getCursorPosition(int *rows, int *cols);

// Append char Buffer
void abAppend(aBuf *ab, const char *s, int len);

// Free char buffer
void abFree(aBuf *ab);

void editorMoveCursor(int key);

void editorOpen(char *fileName);

#endif