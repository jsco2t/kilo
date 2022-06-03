#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

// -----------------------------------------
// Macros
// -----------------------------------------
#define CTRL_KEY(k) ((k) & 0x1f)

// -----------------------------------------
// Data
// -----------------------------------------
struct termios orig_term;

// -----------------------------------------
// Terminal Handling
// -----------------------------------------

void exitOnFailure(const char *message) {
    perror(message);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term) != 0) { exitOnFailure("tcsetattr"); }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_term) != 0) { exitOnFailure("tcgetattr"); }
    atexit(disableRawMode);

    struct termios term = orig_term;

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

// -----------------------------------------
// Input Handling
// -----------------------------------------

void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
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

void editorRefreshScreen() {
    // `\x1b` is a console escape character, it's always followed by `[`. The `J` means clear the screen.
    // The `J` command (following the escape sequence) is controlled by the number before it:
    //  - `0J` means: Clear the screen from the cursor to the end
    //  - `1J` means: Clear the screen from the start of the screen to the cursor
    //  - `2J` means: Clear the entire screen
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // reposition cursor to top of the screen:
    write(STDOUT_FILENO, "\x1b[H", 3); // `H` escape sequence
}

// -----------------------------------------
// Entrypoint
// -----------------------------------------
int main() {
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
