#ifndef EDITOR_H
#define EDITOR_H

#include "termio.h"
#include "time.h"

#define EDITOR_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_TAB_STOP 8
#define EDITOR_QUIT_TIMES 1

enum editorKey
{
    BACKSPACE = 127,
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
    char *render;
    int rsize;
} editorRow;

typedef struct editorConfig
{
    int cx, cy;
    int rx;
    int rowOff;
    int colOff;
    struct termios orig_termios;
    int screenRows;
    int screenCols;
    int numRows;
    editorRow *rows;
    char *filename;
    char statusMsg[80];
    time_t statusMsgTime;
    unsigned int dirty;
} editorConfig;

typedef struct
{
    char *b;
    int len;
} aBuf;

void die(const char *s);
void editorSetStatusMessage(const char *fmt, ...);
void editorUpdateRow(editorRow *row);

#endif