#include <errno.h> // perror()
#include <fcntl.h> // O_RDWR, O_NOCTTY, O_NDELAY, O_SYNC
#include <stdio.h> // printf()
#include <stdlib.h> // EXIT_FAILURE
#include <string.h> // memcpy()
#include <unistd.h> // isatty()
#include <termios.h> // all the tty control functions
#include "argparse.h"

// does this child process then exit when it's done passing back the file descriptor to the parent?
// would such a thing cause problems of using the file descriptor?
// if so then tty attributes should be set in the parent and not in the child
// it is apparently recommended to capture any signals and reset the terminal attributes
// but if we move the attribute setting to the parent, then the child just needs to open
// and pass the file descriptor back to the parent
// one of the problems is where to write the perror or strerror(errno)
// preferably error messages/side-effects should be managed at the top-level
// in this case the main function, however executing perror on the outside
// means you lose context on which actual function caused the error
// so how do you bubble up the errors in such a way that the main function can acquire the actual error point
// it kind of sounds like a custom stacktrace for non-exceptional events
// actually this sounds like wanting exceptions in C
// the main problem is that you may get errno, but you don't know where the errno came from

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

    // here we setup the non-blocking read and write
    // http://stackoverflow.com/a/26006680/582917
    // this can be used for busy-polling or poll and sleep or poll and do something else
    // this still requires O_NONBLOCK setting
    // http://stackoverflow.com/questions/20154157/termios-vmin-vtime-and-blocking-non-blocking-read-operations#comment70719908_22771908
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty_attribs) != 0) {
        perror("tcsetattr()");
        return -1;
    }

    return 0;

}

speed_t select_baud (unsigned int selected_baud) {

    speed_t baud;

    switch (selected_baud) {
        case 50:
            baud = B50;
        break;
        case 75:
            baud = B75;
        break;
        case 110:
            baud = B110;
        break;
        case 134:
            baud = B134;
        break;
        case 150:
            baud = B150;
        break;
        case 200:
            baud = B200;
        break;
        case 300:
            baud = B300;
        break;
        case 600:
            baud = B600;
        break;
        case 1200:
            baud = B1200;
        break;
        case 2400:
            baud = B2400;
        break;
        case 4800:
            baud = B4800;
        break;
        case 9600:
            baud = B9600;
        break;
        case 19200:
            baud = B19200;
        break;
        case 38400:
            baud = B38400;
        break;
        case 57600:
            baud = B57600;
        break;
        case 115200:
            baud = B115200;
        break;
        case 128000:
            baud = B128000;
        break;
        case 230400:
            baud = B230400;
        break;
        case 256000:
            baud = B256000;
        break;
        case 460800:
            baud = B460800;
        break;
        case 500000:
            baud = B500000;
        break;
        case 576000:
            baud = B576000;
        break;
        case 921600:
            baud = B921600;
        break;
        case 1000000:
            baud = B1000000;
        break;
        case 1152000:
            baud = B1152000;
        break;
        case 1500000:
            baud = B1500000;
        break;
        case 2000000:
            baud = B2000000;
        break;
        case 2500000:
            baud = B2500000;
        break;
        case 3000000:
            baud = B3000000;
        break;
        default:
            baud = B9600;
    }

    return baud;

}

int main(int argc, const char * * argv) {

    // we have no options, we only have positional parameters
    static const char * const command_usage[] = {
        "open-serial-device [options] [--] <unix-domain-socket-path> <serial-port-path>",
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
        exit(EXIT_FAILURE);
    }

    const char * unix_sock_path = argv_[0];
    const char * serial_port = argv_[1];

    speed_t baud = select_baud(selected_baud);

    // open serial port with options: read write, this process does not control the terminal, non-blocking, and synchronising
    int serial_fd = open(serial_port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_SYNC);
    if (serial_fd < 0) {
        perror("open()");
        exit(EXIT_FAILURE);
    }

    if (!isatty(serial_fd)) {
        // do you have to close the file descriptor here?
        perror("isatty");
        exit(EXIT_FAILURE);
    }

    if (set_tty_attribs(serial_fd, baud) < 0) {
        printf("%s(): failed\n", "set_tty_attribs");
        exit(EXIT_FAILURE);
    }

    // pass the file descriptor back to the parent using the unix domain socket
    // follow this http://stackoverflow.com/a/38318768/582917 for writing data and closing

}
