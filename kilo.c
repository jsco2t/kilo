//#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

// -----------------------------------------
// Defines and Header/Func Signatures
// -----------------------------------------
#define KILO2_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
void clearScreen(); // implemented in console handling code below

typedef struct erow {
    int size;
    char *chars;
} erow;

// -----------------------------------------
// Data
// -----------------------------------------
struct editorConfig {
    int curX; // horizontal
    int curY; // vertical
    int screenRows;
    int screenCols;
    int numRows;
    erow row;
    struct termios orig_term;
};

struct editorConfig E;

enum editorSpecialKey {
    ARROW_LEFT = 1000, // picking a value that's outside the character range this editor supports
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

// -----------------------------------------
// Terminal Handling
// -----------------------------------------
void exitOnFailure(const char *message) {
    clearScreen(); // clear before printing the error
    perror(message);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_term) != 0) { exitOnFailure("tcsetattr"); }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_term) != 0) { exitOnFailure("tcgetattr"); }
    atexit(disableRawMode);

    struct termios term = E.orig_term;

    // TODO: Investigate UTF-8 handling: Ex: enter `😀` results in: `-16 ('�') -97 ('�') -104 ('�') -128 ('�')`
    term.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP); // input flag toggle: disable `flow control` key combo's | disable CR/NL automatic mapping | disable break condition | disable parity checking | disable 8th bit stripping
    term.c_oflag &= ~(OPOST); // output flag toggle: disable output post processing
    term.c_cflag |= (CS8); //  character toggle: set character size to 8 bits per byte
    term.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); // local/general flag toggle: disable `echo` | enable raw mode | disable terminate key-combos | disable other control key-combos

    term.c_cc[VMIN] = 0; // control character: minimum num of bytes read has to read before it can return
    term.c_cc[VTIME] = 1; // control character: max time to wait before read returns in tenths of seconds (1 == 100ms)

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) != 0) { exitOnFailure("tcsetattr"); }
}

int editorTranslateEscapeSequence() {
    char seq[3];
    char defaultResult = '\x1b';

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
        return defaultResult;
    }

    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
        return defaultResult;
    }

    // key presses for movement keys are sent as an escape sequence:
    //  \x1b[A == UP ARROW
    //  \x1b[B == DOWN ARROW
    //  \x1b[C == RIGHT ARROW
    //  \x1b[D == LEFT ARROW
    //  \x1b[5~ == PAGE UP
    //  \x1b[6~ == PAGE DOWN
    //  \x1b[1~ == HOME
    //  \x1b[7~ == HOME
    //  \x1b[H == HOME
    //  \x1b[OH == HOME
    //  \x1b[4~ == END
    //  \x1b[8~ == END
    //  \x1b[F == END
    //  \x1b[OF == END
    //  \x1b[3~ == DEL
    if (seq[0] == '[') {
        //printf("Key: %c", seq[1]);
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                return defaultResult;
            }

            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                }
            }
        } else {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
    }

    return defaultResult;
}

int editorReadKey() {
    ssize_t nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) { exitOnFailure("read"); }
    }

    if (c == '\x1b') {
        return editorTranslateEscapeSequence();
    }

    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buffer[32];
    unsigned int i = 0;

    // the escape command of `n` asks for device status, the parameter `6` (sent as `6n`) asks for the cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buffer) -1) {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1) {
            break;
        }

        if (buffer[i] == 'R') {
            break;
        }

        i++;
    }

    buffer[i] = '\0';
    if (buffer[0] != '\x1b' || buffer[1] != '[') {
        return -1;
    }
    if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // TIOCGWINSZ = ? Terminal IOCTL Get Win Size
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // fallback solution if `ioctl` read fails

        // Escape sequence + C command = move cursor forward - or to the right
        // Escape sequence + B command = move cursor down
        // Escape sequence C and B are documented to stop at edge/end of screen
        // 999 is an arbitrarily large value - goal is to move the cursor to the bottom/right corner of the screen
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }

        // if we could move the cursor - then use the cursor position to determine the rows/cols
        return getCursorPosition(rows, cols);
    } else { // getting TIOCGWINSZ succeeded
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// -----------------------------------------
// Buffers
// -----------------------------------------
struct strBuffer {
    char *b;
    int len;
};

#define STRBUFFER_INIT {NULL, 0};

void sbAppend(struct strBuffer *strBuf, const char *s, int len) {
    // create new or resize existing buffer - content of updated buffer will start with value(s) from pointer
    char *newBuf = realloc(strBuf->b, strBuf->len + len);

    // memory allocation failure?
    if (newBuf == NULL) {
        return;
    }

    // copy new buffer content starting at end of prior buffer content
    memcpy(&newBuf[strBuf->len], s, len);
    strBuf->b = newBuf; // set new buffer to be result buffer
    strBuf->len += len; // make sure buffer length is correct
}

void sbFree(struct strBuffer *strBuf) {
    free(strBuf->b);
}

// -----------------------------------------
// Input Handling
// -----------------------------------------
void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.curX > 0){
                E.curX--;
            }
            break;

        case ARROW_RIGHT:
            if (E.curX < E.screenCols) {
                E.curX++;
            }
            break;

        case ARROW_UP:
            if (E.curY > 0) {
                E.curY--;
            }
            break;

        case ARROW_DOWN:
            if (E.curY < E.screenRows) {
                E.curY++;
            }
            break;

        case HOME_KEY:
            E.curX = 0;
            break;

        case END_KEY:
            E.curX = E.screenCols - 1;
            break;

        case PAGE_UP:
            E.curY = 0;
            break;

        case PAGE_DOWN:
            E.curY = E.screenRows - 1;
            break;

//        case PAGE_UP:
//        case PAGE_DOWN:
//            {
//                int rows = E.screenRows;
//                while (rows--) {
//                    editorMoveCursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
//                }
//            }
//            break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();

    if (c == CTRL_KEY('q')) {
        clearScreen();
        exit(0);
    } else if (c == ARROW_UP
            || c == ARROW_DOWN
            || c == ARROW_LEFT
            || c == ARROW_RIGHT
            || c == PAGE_UP
            || c == PAGE_DOWN
            || c == HOME_KEY
            || c == END_KEY) {
        editorMoveCursor(c);
    }
}

// -----------------------------------------
// Output Handling
//
// Uses https://en.wikipedia.org/wiki/VT100
// escape sequences. Code will only work
// with terminal emulators which support
// VT100.
//
// To support a wider set of displays
// the `ncurses` library could be used.
// -----------------------------------------
void editorWriteStrBuffer(struct strBuffer *strBuf) {
    write(STDOUT_FILENO, strBuf->b, strBuf->len);
}

void editorClearScreen(struct strBuffer *strBuf) {
    // <esc>[2J = clear whole display, `1J` = clear from start to cursor, `0J` = clear from cursor to end
    sbAppend(strBuf, "\x1b[2J", 4); // erase in display escape sequence: http://vt100.net/docs/vt100-ug/chapter3.html#ED
    sbAppend(strBuf, "\x1b[H", 3); // cursor position escape sequence, default top;right: http://vt100.net/docs/vt100-ug/chapter3.html#CUP
}

void clearScreen() {
    struct strBuffer strBuf = STRBUFFER_INIT;
    editorClearScreen(&strBuf);
    editorWriteStrBuffer(&strBuf);
    sbFree(&strBuf);
}

void editorWriteWelcome(struct strBuffer *strBuf){
    char welcome[80];
    int welcomeLen = snprintf(welcome, sizeof(welcome), "## kilo2 -- version %s ##", KILO2_VERSION);

    if (welcomeLen > E.screenCols) {
        welcomeLen = E.screenCols;
    }

    int padding = (E.screenCols - welcomeLen) / 2;
    if (padding && padding > 0) {
        sbAppend(strBuf, "~", 1);
        padding--;
    }

    if (padding > 0) {
        while (padding--) {
            sbAppend(strBuf, " ", 1);
        }
    }

    sbAppend(strBuf, welcome, welcomeLen);
}

void editorDrawRows(struct strBuffer *strBuf) {
    for (int y = 0; y < E.screenRows; y++) {

        if (y >= E.numRows) {
            if (y == E.screenRows / 3) {
                editorWriteWelcome(strBuf);
            } else {
                sbAppend(strBuf, "~", 1);
            }
        } else {
            sbAppend(strBuf, E.row.chars, E.row.size > E.screenCols ? E.screenCols : E.row.size);
        }


        // <esc>[0K or <esc>[K (default) erase line to right of cursor, `1K` erase line to left of cursor, `2K` erase whole line
        sbAppend(strBuf, "\x1b[K", 3); // erase in line escape sequence: http://vt100.net/docs/vt100-ug/chapter3.html#EL
        if (y < E.screenRows - 1) {
            sbAppend(strBuf, "\r\n", 2);
        }
    }
}

void editorResetCursorToHome(struct strBuffer *strBuf) {
    sbAppend(strBuf, "\x1b[H", 3);
}

void editorSetCursorPosition(struct strBuffer *strBuf) {
    //sbAppend(strBuf, "\x1b[H", 3); // cursor position escape sequence, default top;right: http://vt100.net/docs/vt100-ug/chapter3.html#CUP
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", E.curY + 1, E.curX + 1);
    sbAppend(strBuf, buffer, strlen(buffer));
}

void editorHideCursor(struct strBuffer *strBuf) {
    sbAppend(strBuf, "\x1b[?25l", 6); // hide cursor escape sequence: https://vt100.net/docs/vt510-rm/DECTCEM.html
}

void editorShowCursor(struct strBuffer *strBuf) {
    sbAppend(strBuf, "\x1b[?25h", 6); // show cursor escape sequence: https://vt100.net/docs/vt510-rm/DECTCEM.html
}

void editorRefreshScreen() {
    struct strBuffer strBuf = STRBUFFER_INIT;

    editorHideCursor(&strBuf);
    editorResetCursorToHome(&strBuf);
    //editorClearScreen(&strBuf);
    editorDrawRows(&strBuf);
    editorSetCursorPosition(&strBuf);
    editorShowCursor(&strBuf);
    editorWriteStrBuffer(&strBuf);
    sbFree(&strBuf);

    // @ https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html#clear-lines-one-at-a-time
}

// -----------------------------------------
// File I/O Handling
// -----------------------------------------
void editorOpen() {
    char *line = "Hello, world!";
    int linelen = 13;

    E.row.size = linelen;
    E.row.chars = malloc(linelen + 1);
    memcpy(E.row.chars, line, linelen);
    E.row.chars[linelen] = '\0';
    E.numRows = 1;
}

// -----------------------------------------
// Entrypoint
// -----------------------------------------
void initEditor() {
    E.curX = 0;
    E.curY = 0;
    E.numRows = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) {
        exitOnFailure("getWindowSize");
    }
}

int main() {
    enableRawMode();
    initEditor();
    editorOpen();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
