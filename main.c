#define _GNU_SOURCE
#include "main.h"
#include "stdio.h"
#include "unistd.h"
#include "stdlib.h"
#include "sys/ioctl.h"
#include "ctype.h"
#include "errno.h"
#include "string.h"
#include "sys/types.h"
#include "stdarg.h"

// Data
editorConfig E;

// Row operations

int editorRowCxToRx(editorRow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
            rx += (CUSTOM_EDITOR_TAB_STOP - 1) - (rx % CUSTOM_EDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

void editorUpdateRow(editorRow *row)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (CUSTOM_EDITOR_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % CUSTOM_EDITOR_TAB_STOP != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len)
{
    E.rows = realloc(E.rows, sizeof(editorRow) * (E.numRows + 1));
    int at = E.numRows;
    E.rows[at].size = len;
    E.rows[at].chars = malloc(len + 1);
    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';

    E.rows[at].rsize = 0;
    E.rows[at].render = NULL;
    editorUpdateRow(&E.rows[at]);

    E.numRows++;
}

// IO operations

void editorOpen(char *fileName)
{
    free(E.filename);
    E.filename = strdup(fileName);

    FILE *fp = fopen(fileName, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t lineCap = 0;
    ssize_t lineLen;

    while ((lineLen = getline(&line, &lineCap, fp)) != -1)
    {
        while (lineLen > 0 && (line[lineLen - 1] == '\n' ||
                               line[lineLen - 1] == '\r'))
            lineLen--;

        editorAppendRow(line, lineLen);
    }
    free(line);
    fclose(fp);
}

// Append Buffer

void abAppend(aBuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(aBuf *ab)
{
    free(ab->b);
}

// Terminal

void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(EXIT_FAILURE);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '1':
                    case '7':
                        return HOME_KEY;
                    case '4':
                    case '8':
                        return END_KEY;
                    case '3':
                        return DELETE_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// Input

void editorMoveCursor(int key)
{

    editorRow *row = (E.cy >= E.numRows) ? NULL : &E.rows[E.cy];

    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
            E.cx--;
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.rows[E.cy].size - 1;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size)
            E.cx++;
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.numRows - 1)
            E.cy++;
        break;
    }

    row = (E.cy >= E.numRows) ? NULL : &E.rows[E.cy];
    int rowLen = row ? row->size : 0;
    if (E.cx > rowLen)
        E.cx = rowLen;
}

void editorProcessKeypress()
{
    int c = editorReadKey();
    switch (c)
    {
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
        editorMoveCursor(c);
        break;
    case PAGE_UP:
        E.cy = 0;
        break;
    case PAGE_DOWN:
        if (E.cy < E.numRows)
            E.cy = E.numRows - 1;
        if (E.cx > E.rows[E.cy].size)
            E.cx = E.rows[E.cy].size;
        break;
    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        E.cx = E.rows[E.cy].size;
        break;
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(EXIT_SUCCESS);
        break;
    }
}

// Output

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, ap);
    va_end(ap);
    E.statusMsgTime = time(NULL);
}

void editorDrawMessageBar(aBuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusMsg);
    if (msglen > E.screenCols)
        msglen = E.screenCols;
    if (msglen && time(NULL) - E.statusMsgTime < 5)
        abAppend(ab, E.statusMsg, msglen);
}

void editorDrawStatusBar(aBuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rStatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
                       E.filename ? E.filename : "[No Name]", E.numRows);
    int rLen = snprintf(rStatus, sizeof(rStatus), "%d/%d",
                        E.cy + 1, E.numRows);

    if (len > E.screenCols)
        len = E.screenCols;

    abAppend(ab, status, len);

    while (len < E.screenCols)
    {
        if (E.screenCols - len == rLen)
        {
            abAppend(ab, rStatus, rLen);
            break;
        }
        else
        {

            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numRows)
        E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);

    if (E.cy < E.rowOff)
        E.rowOff = E.cy;
    if (E.cy >= E.rowOff + E.screenRows)
        E.rowOff = E.cy - E.screenRows + 1;

    if (E.rx < E.colOff)
        E.colOff = E.rx;
    if (E.rx >= E.colOff + E.screenCols)
        E.colOff = E.rx - E.screenCols + 1;
}

void editorDrawRows(aBuf *ab)
{
    for (int y = 0; y < E.screenRows; y++)
    {
        int fileRow = y + E.rowOff;
        if (fileRow >= E.numRows)
        {

            if (E.numRows == 0 && y == E.screenRows / 3)
            {
                char welcome[80];
                int wmLen = snprintf(welcome, sizeof(welcome),
                                     "Custom Editor -- version %s", CUSTOM_EDITOR_VERSION);
                if (wmLen > E.screenCols)
                    wmLen = E.screenCols;
                int padding = (E.screenCols - wmLen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, wmLen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.rows[fileRow].rsize - E.colOff;
            if (len < 0)
                len = 0;
            if (len > E.screenCols)
                len = E.screenCols;

            char *chars = malloc(len * sizeof(char));
            for (int i = 0; i < len; i++)
                chars[i] = E.rows[fileRow].render[i + E.colOff];

            abAppend(ab, chars, len);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    editorScroll();

    aBuf ab = {.b = NULL, .len = 0};

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff) + 1, (E.rx - E.colOff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

// init

void initEditorConfig()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowOff = 0;
    E.colOff = 0;
    E.numRows = 0;
    E.rows = NULL;
    E.filename = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsgTime = 0;
    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("getWindowSize");

    E.screenRows -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditorConfig();
    if (argc >= 2)
        editorOpen(argv[1]);

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return EXIT_SUCCESS;
}