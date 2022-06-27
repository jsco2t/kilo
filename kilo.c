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
// Macros and Header/Func Signatures
// -----------------------------------------
#define CTRL_KEY(k) ((k) & 0x1f)
void clearScreen(); // implemented in console handling code below

// -----------------------------------------
// Data
// -----------------------------------------
struct editorConfig {
    int screenRows;
    int screenCols;
    struct termios orig_term;
};

struct editorConfig E;

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

char editorReadKey() {
    ssize_t nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) { exitOnFailure("read"); }
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
void editorProcessKeypress() {
    char c = editorReadKey();

    if (c == CTRL_KEY('q')) {
        clearScreen();
        exit(0);
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
    sbAppend(strBuf, "\x1b[2J", 4);
    sbAppend(strBuf, "\x1b[H", 3);
}

void clearScreen() {
    struct strBuffer strBuf = STRBUFFER_INIT;
    editorClearScreen(&strBuf);
    editorWriteStrBuffer(&strBuf);
    sbFree(&strBuf);
}

void editorDrawRows(struct strBuffer *strBuf) {
    for (int y = 0; y < E.screenRows; y++) {
        sbAppend(strBuf, "~", 1);

        if (y < E.screenRows - 1) {
            sbAppend(strBuf, "\r\n", 2);
        }
    }
}

void editorResetCursorToHome(struct strBuffer *strBuf) {
    sbAppend(strBuf, "\x1b[H", 3); // reposition cursor to top of the screen
}

void editorHideCursor(struct strBuffer *strBuf) {
    sbAppend(strBuf, "\x1b[?25l", 6);
}

void editorShowCursor(struct strBuffer *strBuf) {
    sbAppend(strBuf, "\x1b[?25h", 6);
}

void editorRefreshScreen() {
    // `\x1b` is a console escape character, it's always followed by `[`. The `J` means clear the screen.
    // The `J` command (following the escape sequence) is controlled by the number before it:
    //  - `0J` means: Clear the screen from the cursor to the end
    //  - `1J` means: Clear the screen from the start of the screen to the cursor
    //  - `2J` means: Clear the entire screen
    // Reposition cursor to top of the screen.
    struct strBuffer strBuf = STRBUFFER_INIT;

    editorHideCursor(&strBuf);
    editorClearScreen(&strBuf);
    editorDrawRows(&strBuf);
    editorResetCursorToHome(&strBuf);
    editorShowCursor(&strBuf);
    editorWriteStrBuffer(&strBuf);
    sbFree(&strBuf);

    // @ https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html#clear-lines-one-at-a-time
}

// -----------------------------------------
// Entrypoint
// -----------------------------------------
void initEditor() {
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) {
        exitOnFailure("getWindowSize");
    }
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
