#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <sysexits.h>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <string.h>

#include <sys/socket.h>
#include <linux/un.h>

#include <assert.h>

#include "argparse/argparse.h"
#include "baudrates.h"
#include "protocol.h"

static ssize_t ssize;

static int
set_tty_attribs (int fd, speed_t speed) {

  // get the current attributes
  struct termios tty_attribs;
  if (tcgetattr(fd, &tty_attribs) < 0) {
    return 0;
  }

  // only set what we want to change for the current attributes

  // set input and output baud rate
  cfsetospeed(&tty_attribs, speed);
  cfsetispeed(&tty_attribs, speed);

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
  tty_attribs.c_cc[VMIN] = 0;
  tty_attribs.c_cc[VTIME] = 0;

  // set the modified attributes
  if (tcsetattr(fd, TCSANOW, &tty_attribs) != 0) {
    return -1;
  }

  return 1;

}

speed_t
select_baud (unsigned int selected_baud) {

  speed_t baud;
  #define BAUDDEFAULT(TARGET) default: TARGET = 9600;
  BAUDSWITCH(selected_baud, baud, BAUDDEFAULT)
  return baud;

}

int
main (int argc, const char * const * argv) {

  static const char * const command_usage[] = {
    "open-serial-device [--] <serial-port-path> <baud> <unix-domain-socket-path>",
    NULL,
  };

  struct argparse_option command_options[] = {
    OPT_HELP(),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, command_options, command_usage, 0);
  argparse_describe(&argparse, "\nThis is to be executed as a child process. It will open the serial port and pass the file descriptor back to the parent process through the unix domain socket.", "");

  const char * * argv_ = malloc(sizeof (char *) * argc);
  memcpy((char * *) argv_, argv, sizeof(char *) * argc);

  int argc_ = argparse_parse(&argparse, argc, argv_);

  if (argc_ < 3) {
      argparse_usage(&argparse);
      exit(EX_USAGE);
  }

  const char * serial_port = argv_[0];
  unsigned int desired_baud = (unsigned int) strtol(argv[1], (char * *) NULL, 10);
  const char * unix_sock_path = argv_[2];

  speed_t baud = select_baud(desired_baud);

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
    fprintf(stderr, "%s\n", "Serial port path does not open to a serial port");
    exit(EX_NOINPUT);
  }

  if (set_tty_attribs(serial_fd, baud) < 0) {
    fprintf(stderr, "%s\n", "Could not set tty attributes");
    exit(EX_OSERR);
  }

  int unix_sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (unix_sock_fd < 0) {
    perror("socket()");
    exit(EX_OSERR);
  }

  struct sockaddr_un unix_sock_addr;
  unix_sock_addr.sun_family = AF_UNIX;
  snprintf(unix_sock_addr.sun_path, UNIX_PATH_MAX, "%s", unix_sock_path);

  if (
    connect(
      unix_sock_fd,
      (struct sockaddr *) &unix_sock_addr,
      sizeof(unix_sock_addr)
    ) != 0
  ) {
    perror("connect()");
    exit(EX_OSERR);
  }

  MechanismProto message = {
    .type = PRIVFD
  };

  uint8_t type_buffer[1] = {0};
  type_buffer[0] = message.type;

  char message_buffer[sizeof(type_buffer)] = {0};
  memcpy(message_buffer, type_buffer, sizeof(type_buffer));

  printf("Child Process Buffer!\n");
  for (int i = 0; i < sizeof(message_buffer); ++i) {
    printf("0x%02X", message_buffer[i]);
  }
  printf("\n");

  struct iovec io_vector[1] = {
    {
      .iov_base = message_buffer,
      .iov_len = sizeof(message_buffer)
    }
  };

  char ancillary_buffer[CMSG_SPACE(sizeof(int))] = {0};

  struct msghdr message_options = {0};
  message_options.msg_iov = io_vector;
  message_options.msg_iovlen = sizeof(io_vector);
  message_options.msg_control = ancillary_buffer;
  message_options.msg_controllen = sizeof(ancillary_buffer);

  struct cmsghdr * control_message = CMSG_FIRSTHDR(&message_options);
  control_message->cmsg_level = SOL_SOCKET;
  control_message->cmsg_type = SCM_RIGHTS;
  control_message->cmsg_len = CMSG_LEN(sizeof(int));

  int * ancillary_p = (int *) CMSG_DATA(control_message);
  *ancillary_p = serial_fd;

  printf("Child Process SEND!\n");
  ssize = sendmsg(unix_sock_fd, &message_options, 0);
  printf("Child Process FIN SEND!\n");

  if (ssize < sizeof(message_buffer)) {
    perror("recvmsg()");
    exit(EX_OSERR);
  }

  printf("Child Process FIN FIN!\n");

  exit(EXIT_SUCCESS);

}
