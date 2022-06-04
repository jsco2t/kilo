#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
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

        // Escape sequence + C command = move cursor to the right
        // Escape sequence + B command = move cursor down
        // 999 is an arbitrarily large value - goal is to move the cursor to the bottom/right corner of the screen
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// -----------------------------------------
// Input Handling
// -----------------------------------------
void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            clearScreen();
            exit(0);
            break;
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
void clearScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorDrawRows() {
    int y;

    for (y = 0; y < E.screenRows; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {
    // `\x1b` is a console escape character, it's always followed by `[`. The `J` means clear the screen.
    // The `J` command (following the escape sequence) is controlled by the number before it:
    //  - `0J` means: Clear the screen from the cursor to the end
    //  - `1J` means: Clear the screen from the start of the screen to the cursor
    //  - `2J` means: Clear the entire screen
    //write(STDOUT_FILENO, "\x1b[2J", 4);

    // reposition cursor to top of the screen:
    //write(STDOUT_FILENO, "\x1b[H", 3); // `H` escape sequence

    clearScreen();
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
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
