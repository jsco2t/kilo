#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

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

// -----------------------------------------
// Entrypoint
// -----------------------------------------
int main() {
    enableRawMode();

    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) { exitOnFailure("read"); } // per doc `-1` is the error return value

        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c); // since `output flag` of `OPOST` is disabled, we have to manually return CR LF, instead of just LF
        }

        if (c == 'q') break;
    }

    return 0;
}
