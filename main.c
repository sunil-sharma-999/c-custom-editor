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
#include "fcntl.h"

// Data
editorConfig E;

// Row operations

void editorFreeRow(editorRow *row)
{
    free(row->chars);
    free(row->render);
    free(row->hl);
}

void editorDeleteRow(int at)
{
    if (at < 0 || at >= E.numRows)
        return;
    editorFreeRow(&E.rows[at]);
    memmove(&E.rows[at], &E.rows[at + 1], sizeof(editorRow) * (E.numRows - at - 1));
    E.numRows--;
    E.dirty++;
}

void editorRowAppendString(editorRow *row, char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

int editorRowCxToRx(editorRow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
            rx += (EDITOR_TAB_STOP - 1) - (rx % EDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(editorRow *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
            cur_rx += (EDITOR_TAB_STOP - 1) - (cur_rx % EDITOR_TAB_STOP);
        cur_rx++;
        if (cur_rx > rx)
            return cx;
    }
    return cx;
}

void editorUpdateRow(editorRow *row)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc((row->size + tabs) * (EDITOR_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % EDITOR_TAB_STOP != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len)
{
    E.rows = realloc(E.rows, sizeof(editorRow) * (E.numRows + 1));
    memmove(&E.rows[at + 1], &E.rows[at], sizeof(editorRow) * (E.numRows - at));

    E.rows[at].size = len;
    E.rows[at].chars = malloc(len + 1);
    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';

    E.rows[at].rsize = 0;
    E.rows[at].render = NULL;
    E.rows[at].hl = NULL;
    editorUpdateRow(&E.rows[at]);

    E.numRows++;
    E.dirty++;
}

void editorRowInsertChar(editorRow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
}

void editorInsertChar(int c)
{
    if (E.cy == E.numRows)
        editorInsertRow(E.numRows, "", 0);
    editorRowInsertChar(&E.rows[E.cy], E.cx, c);
    E.cx++;
    E.dirty++;
}

void editorInsertNewline()
{
    if (E.cx == 0)
    {
        editorInsertRow(E.cy, "", 0);
    }
    else
    {
        editorRow *row = &E.rows[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.rows[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorRowDelChar(editorRow *row, int at)
{
    if (at < 0 || at >= row->size)
        return;

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at - 1);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorDelChar()
{
    if (E.cy == E.numRows)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;

    editorRow *row = &E.rows[E.cy];

    if (E.cx > 0)
    {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    }
    else
    {
        E.cx = E.rows[E.cy - 1].size;
        editorRowAppendString(&E.rows[E.cy - 1], row->chars, row->size);
        editorDeleteRow(E.cy);
        E.cy--;
    }
}

// file IO operations

char *editorRowsToString(int *buflen)
{
    int length = 0;
    int j;
    for (j = 0; j < E.numRows; j++)
        length += E.rows[j].size + 1;
    *buflen = length;
    char *buf = malloc(length);
    char *p = buf;
    for (j = 0; j < E.numRows; j++)
    {
        memcpy(p, E.rows[j].chars, E.rows[j].size);
        p += E.rows[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorSave()
{
    if (E.filename == NULL)
    {
        E.filename = editorPrompt("Save as: %s", NULL);
        if (E.filename == NULL)
        {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }
    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1)
    {
        editorSetStatusMessage("Error while saving file");
    }
    else
    {
        if (ftruncate(fd, len) != -1)
        {
            write(fd, buf, len);
            close(fd);
            free(buf);
            editorSetStatusMessage("%d bytes written to disk", len);
            E.dirty = 0;
            return;
        }
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

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

        editorInsertRow(E.numRows, line, lineLen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
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

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';
    while (1)
    {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();
        int c = editorReadKey();
        if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        {
            if (buflen != 0)
                buf[--buflen] = '\0';
        }
        else if (c == '\x1b')
        {
            editorSetStatusMessage("");
            if (callback)
                callback(buf, c);
            free(buf);
            return NULL;
        }
        else if (c == '\r')
        {
            if (buflen != 0)
            {
                editorSetStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128)
        {
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if (callback)
            callback(buf, c);
    }
}

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
    static int quitTimes = EDITOR_QUIT_TIMES;
    int c = editorReadKey();
    switch (c)
    {
    case '\r':
        editorInsertNewline();
        break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DELETE_KEY:
        if (c == DELETE_KEY)
            editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
    case CTRL_KEY('s'):
        if (E.dirty)
            editorSave();
        break;
    case CTRL_KEY('f'):
        editorFind();
        break;

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
        if (E.dirty && quitTimes > 0)
        {
            editorSetStatusMessage("File has unsaved changes. "
                                   "Press Ctrl-Q %d more time to quit.",
                                   quitTimes);
            quitTimes--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        for (int i = 0; i < E.numRows; i++)
        {
            editorFreeRow(&E.rows[i]);
        }
        free(E.rows);
        free(E.filename);

        exit(EXIT_SUCCESS);
        break;
    case CTRL_KEY('l'):
    case '\x1b':
        break;
    default:
        if (!iscntrl(c))
            editorInsertChar(c);
        break;
    }

    quitTimes = EDITOR_QUIT_TIMES;
}

// Syntax highlighting

int is_separator(int c)
{
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

int editorSyntaxToColor(int hl)
{
    switch (hl)
    {
    case HL_NUMBER:
        return 31;
    case HL_MATCH:
        return 34;
    default:
        return 37;
    }
}

void editorUpdateSyntax(editorRow *row)
{
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    int prevSep = 1;

    int i = 0;
    while (i < row->rsize)
    {
        char c = row->render[i];
        unsigned char prevHL = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
        if ((isdigit(c) && (prevSep || prevHL == HL_NUMBER)) ||
            (c == '.' && prevHL == HL_NUMBER))
        {
            row->hl[i] = HL_NUMBER;
            i++;
            prevSep = 0;
            continue;
        }
        prevSep = is_separator(c);
        i++;
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
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]", E.numRows,
                       E.dirty ? "(modified)" : "");

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
                                     "Custom Editor -- version %s", EDITOR_VERSION);
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
            char *s = &E.rows[fileRow].render[E.colOff];
            unsigned char *hl = &E.rows[fileRow].hl[E.colOff];
            int j;
            int currentColor = -1;
            for (j = 0; j < len; j++)
            {
                if (hl[j] == HL_NORMAL)
                {
                    if (currentColor != -1)
                    {
                        abAppend(ab, "\x1b[39m", 5);
                        currentColor = -1;
                    }
                    abAppend(ab, &s[j], 1);
                }
                else
                {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != currentColor)
                    {

                        currentColor = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &s[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
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

// find feature

void editorFindCallback(char *query, int key)
{

    static int lastMatch = -1;
    static int direction = 1;

    static int savedLineHL;
    static char *savedHL = NULL;
    if (savedHL)
    {
        memcpy(E.rows[savedLineHL].hl, savedHL, E.rows[savedLineHL].rsize);
        free(savedHL);
        savedHL = NULL;
    }

    if (key == '\r' || key == '\x1b')
    {
        lastMatch = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
        direction = 1;
    else if (key == ARROW_LEFT || key == ARROW_UP)
        direction = -1;
    else
    {
        lastMatch = -1;
        direction = 1;
    }

    if (lastMatch == -1)
        direction = 1;
    int current = lastMatch;

    int i;
    for (i = 0; i < E.numRows; i++)
    {
        current += direction;
        if (current == -1)
            current = E.numRows - 1;
        else if (current == E.numRows)
            current = 0;

        editorRow *row = &E.rows[current];
        char *match = strstr(row->render, query);
        if (match)
        {
            lastMatch = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);

            savedLineHL = current;
            savedHL = malloc(row->rsize);
            memcpy(savedHL, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));

            if (E.screenRows < E.numRows)
                E.rowOff = E.numRows;
            if (E.cx < E.screenCols)
                E.colOff = 0;
            else
                E.colOff = E.cx;
            break;
        }
    }
}

void editorFind()
{

    int savedCX = E.cx;
    int savedCY = E.cy;
    int savedColOff = E.colOff;
    int savedRowOff = E.rowOff;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
    if (query)
    {
        free(query);
    }
    else
    {
        E.cx = savedCX;
        E.cy = savedCY;
        E.colOff = savedColOff;
        E.rowOff = savedRowOff;
    }
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
    E.dirty = 0;
    editorSetStatusMessage(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

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