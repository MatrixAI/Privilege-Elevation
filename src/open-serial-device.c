#include <errno.h> // perror()
#include <sysexits.h>
#include <fcntl.h> // O_RDWR, O_NOCTTY, O_NDELAY, O_SYNC
#include <stdio.h> // stderr, printf()
#include <stdlib.h> // EXIT_FAILURE
#include <string.h> // memcpy()
#include <unistd.h> // isatty()
#include <termios.h> // all the tty control functions
#include "argparse.h"
#include "baudrates.h"

int set_tty_attribs (int fd, speed_t speed) {

    // get the current attributes
    struct termios tty_attribs;
    if (tcgetattr(fd, &tty_attribs) < 0) {
        perror("tcgetattr()");
        return -1;
    }

    // only set what we want to change for the current attributes

    // set input and output baud rate
    cfsetospeed(&tty_attribs, speed);
    cfsetispeed(&tty_attribs, speed);

    // O |= X - is used to turn on X option
    // O &= ~X - is used to turn off X option
    // O ^= X - is used to toggl X option
    // (O & X) == X - is used to check if X is on

    // helper for setting up for non-canonical mode settings
    // this basically means input, line and output processing are all disabled
    // canonical mode is designed for actual terminals, not dumb serial transports
    cfmakeraw(&tty_attribs);

    // ignore modem controls and enable receiver
    tty_attribs.c_cflag |= (CLOCAL | CREAD);
    // only 1 stop bit
    tty_attribs.c_cflag &= ~CSTOPB;
    // disable hardware flow control
    tty_attribs.c_cflag &= ~CRTSCTS;

    // here we setup the non-blocking non-canonical mode
    // this means O_NONBLOCK must not be set on the file descriptor
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    // set the modified attributes
    if (tcsetattr(fd, TCSANOW, &tty_attribs) != 0) {
        perror("tcsetattr()");
        return -1;
    }

    return 0;

}

speed_t select_baud (unsigned int selected_baud) {

    speed_t baud;
    #define BAUDDEFAULT(TARGET) default: TARGET = 9600;
    BAUDSWITCH(selected_baud, baud, BAUDDEFAULT)
    return baud;

}

int main(int argc, const char * * argv) {

    static const char * const command_usage[] = {
        "open-serial-device [options] [--] <serial-port-path> <unix-domain-socket-path>",
        NULL,
    };

    unsigned int selected_baud;

    struct argparse_option command_options[] = {
        OPT_HELP(),
        OPT_INTEGER('b', "baud", &selected_baud, "select standard baud rate, the default is 9600"),
        OPT_END(),
    };

    struct argparse argparse;

    argparse_init(&argparse, command_options, command_usage, 0);
    argparse_describe(&argparse, "\nThis is to be executed as a child process. It will open the serial port and pass the file descriptor back to the parent process through the unix domain socket.");

    // make sure the original argc and argv is preserved
    const char * * argv_ = malloc(sizeof (char *) * argc);
    memcpy(argv_, argv, sizeof(char *) * argc);

    // the argc_ and argv_ has the command name and all parsed options subtracted
    int argc_ = argparse_parse(&argparse, argc, argv_);

    if (argc_ < 2) {
        argparse_usage(&argparse);
        exit(EX_USAGE);
    }

    const char * serial_port = argv_[0];
    const char * unix_sock_path = argv_[1];

    speed_t baud = select_baud(selected_baud);

    // do not open in non-blocking mode when using non-canonical mode
    int serial_fd = open(serial_port, O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_fd < 0) {
        perror("open()");
        if (errno == EACCES) {
            exit(EX_NOPERM);
        } else {
            exit(EX_UNAVAILABLE);
        }
    }

    if (!isatty(serial_fd)) {
        close(serial_fd);
        perror("isatty");
        exit(EX_NOINPUT);
    }

    if (set_tty_attribs(serial_fd, baud) < 0) {
        fprintf(stderr, "%s(): failed\n", "set_tty_attribs");
        exit(EX_OSERR);
    }

    // pass the file descriptor back to the parent using the unix domain socket
    // follow this http://stackoverflow.com/a/38318768/582917 for writing data and closing

}
