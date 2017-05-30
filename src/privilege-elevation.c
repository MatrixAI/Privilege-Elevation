#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#include <errno.h>
#include <sysexits.h>

#include <unistd.h>
#include <fcntl.h>

#include <string.h>
#include <libgen.h>
#include <ftw.h>

#include <signal.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <linux/un.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/prctl.h>

#include <assert.h>

#include "argparse/argparse.h"
#include "protocol.h"

#if !defined(MECHANISM_PATH)
  #error "MECHANISM_PATH must be defined."
#endif

static struct sigaction old_sigint_action;

static int status;
static size_t size;
static ssize_t ssize;

static int unix_sock_fd;
static int unix_peer_fd;
static char * unix_sock_dir;

static volatile sig_atomic_t mechanism_status = -1;

static int
nftw_callback (
  const char * path,
  const struct stat * sb,
  int type_flag,
  struct FTW * ftw_buf
) {

  status = remove(path);

  if (status != 0) {
    char error_string[8 + UNIX_PATH_MAX + 1];
    snprintf(error_string, sizeof(error_string), "remove(%s)", path);
    perror(error_string);
  }

  return status;

}

static void
cleanup_and_exit () {

  if (unix_peer_fd) close(unix_peer_fd);
  if (unix_sock_fd) close(unix_sock_fd);
  if (unix_sock_dir && *unix_sock_dir) {
    nftw(unix_sock_dir, nftw_callback, 64, FTW_DEPTH | FTW_PHYS);
  }

}

static void
cleanup_and_exit_sigint (int signal, siginfo_t * signal_info, void * context) {

  assert(signal == SIGINT);
  cleanup_and_exit();
  sigaction(SIGINT, &old_sigint_action, NULL);
  kill(0, SIGINT);

}

static int
handle (
  int sig,
  void (*handler)(int, siginfo_t *, void *),
  int flags,
  struct sigaction * old_action
) {

  struct sigaction action = {0};
  action.sa_flags = flags;
  action.sa_sigaction = handler;
  return (sigaction(sig, &action, old_action) == 0);

}

/**
 * Blocks SIGCHLD
 * Will assign by reference the original mask prior to blocking.
 */
static int
block_sigchld (sigset_t * signal_orig_mask) {

  sigset_t signal_sigchld_mask;
  sigemptyset(&signal_sigchld_mask);
  sigaddset(&signal_sigchld_mask, SIGCHLD);
  return (sigprocmask(SIG_BLOCK, &signal_sigchld_mask, signal_orig_mask) == 0);

}

static int
unblock_sigchld (sigset_t * signal_orig_mask) {

  return (sigprocmask(SIG_SETMASK, signal_orig_mask, NULL) == 0);

}

static void
record_mechanism_process (int signal, siginfo_t * signal_info, void * context) {

  assert(signal == SIGCHLD);

  switch (signal_info->si_code) {
  case CLD_EXITED:
  case CLD_KILLED:
  case CLD_DUMPED:
    mechanism_status = signal_info->si_status;
  }

}

static bool
check_peer_pid (int peer_sock_fd, pid_t peer_pid) {

  struct ucred peer_credentials;
  int peer_credentials_size = sizeof(peer_credentials);
  if (
    getsockopt(
      peer_sock_fd,
      SOL_SOCKET,
      SO_PEERCRED,
      &peer_credentials,
      &peer_credentials_size
    ) != 0
  ) {
    return false;
  }

  return (peer_credentials.pid == peer_pid);

}

static bool
parse_args (
  int argc,
  const char * const * argv,
  uint32_t * baud,
  const char * * serial_port
) {

  // command, -option, option_value, --, param

  const char * argv_[sizeof(char *) * 5] = {0};
  memcpy((char * *) argv_, argv, sizeof(argv_));

  static const char * const command_usage[] = {
    "privilege-elevation [options] [--] <serial-port-path>",
    NULL,
  };

  struct argparse_option command_options[] = {
    OPT_HELP(),
    OPT_INTEGER(
      'b',
      "baud",
      &baud,
      "select standard baud rate, the default is 9600"
    ),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, command_options, command_usage, 0);

  argparse_describe(&argparse, "\nThis demonstrates lazy privilege elevation via opening a secured serial port resource.", "");

  int argc_ = argparse_parse(&argparse, MIN(argc, 5), argv_);

  if (argc_ < 1) {
    argparse_usage(&argparse);
    return false;
  }

  *serial_port = argv_[0];

  return true;

}

static int
setup_unix_sock (const char * sock_path, int backlog, bool nonblocking) {

  struct sockaddr_un unix_sock_addr;
  unix_sock_addr.sun_family = AF_UNIX;
  snprintf(unix_sock_addr.sun_path, UNIX_PATH_MAX, "%s", sock_path);

  int unix_sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (unix_sock_fd < 0) {
    return -1;
  }

  if (
    bind(
      unix_sock_fd,
      (struct sockaddr *) &unix_sock_addr,
      sizeof(unix_sock_addr)
    ) != 0
  ) {
    return -1;
  }

  // we only expect 1 client, so a backlog of 1 is fine
  if (listen(unix_sock_fd, backlog) != 0) {
    return -1;
  }

  if (nonblocking) {
    int nonblocking_flag = fcntl(unix_sock_fd, F_GETFL, 0) | O_NONBLOCK;
    if (fcntl(unix_sock_fd, F_SETFL, nonblocking_flag) == -1) {
      return -1;
    }
  }

  return unix_sock_fd;

}

static int
exec_mechanism (
  const char * process_path,
  const char * const process_arguments[],
  pid_t * mechanism_pid
) {

  // setup a pipe for between parent and forked child process
  // to communicate errors during the fork prior to the exec
  int exec_pipe[2];
  if (pipe(exec_pipe) != 0) {
    return 0;
  }

  pid_t parent_pid = getpid();
  pid_t child_pid = fork();

  if (child_pid == -1) {

    return -1;

  } else if (child_pid == 0) {

    // close the read side in the child
    close(exec_pipe[0]);

    // if the parent dies, we want the child to commit suicide
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
      // if the parent had died, this would result in a SIGPIPE
      // which close the parent process as well
      if (write(exec_pipe[1], &errno, sizeof(errno)));
      exit(EX_OSERR);
    }

    // what if the parent already died!? If so we must die
    // since there's no parent, there's no point writing to the exec pipe
    if (getppid() != parent_pid) {
      exit(EX_UNAVAILABLE);
    }

    // set the write side to close if the subsequent exec works
    // this doesn't guarantee that the mechanism process succeeds
    // only that the fork + exec worked
    if ((fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC)) == -1) {
      if (write(exec_pipe[1], &errno, sizeof(errno)));
      exit(EX_OSERR);
    }

    // execute the process with the arguments
    // the child process can access file descriptors in the parent
    // however we need to pass file descriptors from the child to the parent
    // so we'll be using unix domain sockets for communication
    execvp(process_path, (char * const *) process_arguments);

    // exec failed, we must write the errno into the pipe
    if (write(exec_pipe[1], &errno, sizeof(errno)));

    // exit the child process
    exit(EX_UNAVAILABLE);

  }

  // close the write end on the parent
  close(exec_pipe[1]);

  // this blocks until we either receive a close or an actual write
  // on close, the size of the read will be 0
  // on write, the size of the read will be > 0
  // we use close to mean successful exec
  // we use write to mean there was an error
  int exec_errno;
  if (read(exec_pipe[0], &exec_errno, sizeof(exec_errno)) > 0) {
    errno = exec_errno;
    return -2;
  }

  // exec succeeded, close the read end
  close(exec_pipe[0]);

  *mechanism_pid = child_pid;

  return 1;

}

static int
wait_for_message (
  int sock_fd,
  sigset_t * signal_mask
) {

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(sock_fd, &readfds);

  while (true) {

    status = pselect(sock_fd + 1, &readfds, NULL, NULL, NULL, signal_mask);
    if (status == -1) {

      if (errno != EINTR) {
        return 0;
      }

      switch (mechanism_status) {
      case EXIT_SUCCESS:
        break;
      case EX_NOPERM:
        return -1;
      default:
        return -2;
      }

      // if not interrupted from the mechanism, just continue

    } else if (status > 0 && FD_ISSET(unix_sock_fd, &readfds)) {

      break;

    }

  }

  return 1;

}

static int
launch_mechanism (
  const char * mechanism_path,
  const char * const * mechanism_args,
  const char * pkexec_path,
  const char * const * pkexec_args,
  int sock_fd,
  sigset_t * signal_mask,
  pid_t * mechanism_pid,
  bool privileged
) {

  if (!privileged) {
    fprintf(stderr, "%s\n", "Attempting to open without elevated privileges");
    status = exec_mechanism(mechanism_path, mechanism_args, mechanism_pid);
  } else {
    fprintf(stderr, "%s\n", "Attempting to open with elevated privileges");
    status = exec_mechanism(pkexec_path, pkexec_args, mechanism_pid);
  }

  switch (status) {
  case 0:
    return 0;
  case -1:
    return -1;
  case  -2:
    return -2;
  }

  status = wait_for_message(sock_fd, signal_mask);

  switch (status) {
  case 1:
    return 1;
  case 0:
    return -3;
  case -1:
    return launch_mechanism(
      mechanism_path,
      mechanism_args,
      pkexec_path,
      pkexec_args,
      sock_fd,
      signal_mask,
      mechanism_pid,
      true
    );
  case -2:
    return -4;
  }

}

int
main (int argc, const char * const * argv) {

  /* SETUP ENVIRONMENT */

  atexit(cleanup_and_exit);
  handle(SIGINT, cleanup_and_exit_sigint, 0, &old_sigint_action);

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  // permanent variables
  const char * tmp_dir= getenv("TMPDIR");
  if (!tmp_dir) tmp_dir = "/tmp";
  const char tmp_name[] = "polkit_demo.XXXXXX";
  const char socket_name[] = "socket.sock";

  char pkexec_path[] = "pkexec";
  char mechanism_path[] = MECHANISM_PATH;

  char pkexec_name[] = "pkexec";
  char mechanism_name[] = MECHANISM_PATH;
  basename(mechanism_name);

  int serial_port_fd = -1;

  uint32_t baud = 0;
  const char * serial_port;

  if (!parse_args(argc, argv, &baud, &serial_port)) {
    exit(EX_USAGE);
  }

  if (!baud) {
    baud = 9600;
  }

  assert(UNIX_PATH_MAX >=
    (
      strlen(tmp_dir) +
      sizeof(tmp_name) +
      sizeof(socket_name) + 1
    )
  );

  char template[strlen(tmp_dir) + (sizeof(tmp_name) + 1)];
  snprintf(template, sizeof(template), "%s/%s", tmp_dir, tmp_name);
  unix_sock_dir = mkdtemp(template);

  if (!unix_sock_dir) {
    perror("create_tmp_namespace()");
    exit(EX_CANTCREAT);
  }

  // the unix_sock_path = unix_sock_dir + socket_name
  char unix_sock_path[UNIX_PATH_MAX];
  snprintf(
    unix_sock_path,
    sizeof(unix_sock_path),
    "%s/%s",
    unix_sock_dir,
    socket_name
  );

  // if an asynchronous network error occurs, accept needs to fail immediately
  // but accept is a slow system call, so it can block indefinitely
  // to prevent this, unix_sock_fd is set to non blocking
  // furthermore we're only expecting one client, so backlog of 1
  unix_sock_fd = setup_unix_sock(unix_sock_path, 1, true);
  if (unix_sock_fd == -1) {
    perror("setup_unix_sock");
    exit(EX_OSERR);
  }

  // setup the signal handler for SIGCHLD
  // this will handle if the child process breaks
  sigset_t signal_orig_mask;
  if (!block_sigchld(&signal_orig_mask)) {
    perror("block_sigchld()");
    exit(EX_OSERR);
  }

  // SA_SIGINFO for acquiring extra child process info
  // SA_NOCLDSTOP because we don't care about suspension or continued signals
  if (!handle(SIGCHLD, record_mechanism_process, SA_SIGINFO | SA_NOCLDSTOP, NULL)) {
    perror("handle()");
    exit(EX_OSERR);
  }

  /* EXECUTION CODE */

  char selected_baud[7];
  snprintf(selected_baud, sizeof(selected_baud), "%u", baud);

  const char * const mechanism_args[] = {
    mechanism_name,
    serial_port,
    selected_baud,
    unix_sock_path,
    (char *) NULL
  };

  const char const * pkexec_args[] = {
    pkexec_name,
    mechanism_path,
    serial_port,
    selected_baud,
    unix_sock_path,
    (char *) NULL
  };

  pid_t mechanism_pid = 0;

  status = launch_mechanism(
    mechanism_path,
    mechanism_args,
    pkexec_path,
    pkexec_args,
    unix_sock_fd,
    &signal_orig_mask,
    &mechanism_pid,
    false
  );

  char error_string[8 + sizeof(mechanism_path) + 1];
  switch (status) {
  case 0:
    perror("pipe()");
    exit(EX_OSERR);
  case -1:
    perror("fork()");
    exit(EX_OSERR);
  case -2:
    snprintf(error_string, sizeof(error_string), "execvp(%s)", mechanism_path);
    perror(error_string);
    exit(EX_OSERR);
  case -3:
    perror("pselect()");
    exit(EX_TEMPFAIL);
  case -4:
    switch (mechanism_status) {
    case 127:
      fprintf(stderr, "Error: %s\n", "Polkit denied permission to elevate privileges");
      exit(EX_NOPERM);
    case 126:
      fprintf(stderr, "Error: %s\n", "User denied permission to elevate privileges");
      exit(EX_USAGE);
    default:
      fprintf(stderr, "Error: %s %i\n", "Mechanism failed with code:", mechanism_status);
      exit(EX_SOFTWARE);
    }
  }

  struct sockaddr_un unix_peer_addr = {0};
  socklen_t unix_peer_addr_size = sizeof(unix_peer_addr);

  // the accepted connection is not non-blocking
  unix_peer_fd = TEMP_FAILURE_RETRY(
    accept(
      unix_sock_fd,
      (struct sockaddr *) &unix_peer_addr,
      &unix_peer_addr_size
    )
  );

  if (unix_peer_fd == -1) {
    perror("accept()");
    exit(EX_OSERR);
  }

  close(unix_sock_fd);

  if (!check_peer_pid(unix_peer_fd, mechanism_pid)) {
    fprintf(stderr, "Error: %s\n", "Unknown peer pid");
    exit(EX_PROTOCOL);
  }

  shutdown(unix_peer_fd, SHUT_WR);

  /* RECEIVE CODE */

  // message is an array of 1 MechanismProto
  MechanismProto message[1] = {0};
  char message_buffer[sizeof(message)] = {0};
  struct iovec io_vector[1] = {{
      .iov_base = message_buffer,
      .iov_len = sizeof(message_buffer)
    }
  };

  // suitably aligned ancillary data
  union {
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } ancillary_buffer;

  struct msghdr message_options = {0};
  message_options.msg_iov = io_vector;
  message_options.msg_iovlen = 1;
  message_options.msg_control = ancillary_buffer.buf;
  message_options.msg_controllen = sizeof(ancillary_buffer.buf);

  if (!unblock_sigchld(&signal_orig_mask)) {
    perror("unblock_sigchld()");
    exit(EX_OSERR);
  }

  while (true) {

    ssize = recvmsg(unix_peer_fd, &message_options, MSG_WAITALL);

    if (ssize == -1) {

      if (errno == EINTR) {

        if (mechanism_status != -1 && mechanism_status != EXIT_SUCCESS) {
          fprintf(stderr, "Error: %s %i\n", "Mechanism failed after connecting with code:", mechanism_status);
          exit(EX_UNAVAILABLE);
        }

      } else {

        perror("recvmsg()");
        exit(EX_OSERR);

      }

    } else if ((size_t) ssize < sizeof(message_buffer)) {

      fprintf(stderr, "recvmsg(): %s\n", "Received incorrect message size from mechanism");
      exit(EX_PROTOCOL);

    } else {

      break;

    }

  }

  // reinterpreting message buffer as message
  memcpy(message, message_buffer, sizeof(message));

  printf("Received Data:");
  for (int i = 0; i < sizeof(message_buffer); ++i) {
    printf(" 0x%02X", message_buffer[i]);
  }
  printf("\n");

  if (message[0].type != PRIVFD) {
    fprintf(stderr, "Error: %s\n", "Unexpected message from mechanism");
    exit(EX_PROTOCOL);
  }

  if ((message_options.msg_flags & MSG_CTRUNC) == MSG_CTRUNC) {
    fprintf(stderr, "Error: %s\n", "Not enough space provided for ancillary data");
    exit(EX_SOFTWARE);
  }

  struct cmsghdr * ancillary_message = CMSG_FIRSTHDR(&message_options);

  if (
    ancillary_message->cmsg_level == SOL_SOCKET &&
    ancillary_message->cmsg_type == SCM_RIGHTS
  ) {
    int * ancillary_p = (int *) CMSG_DATA(ancillary_message);
    serial_port_fd = *ancillary_p;
  } else {
    fprintf(stderr, "Error: %s\n", "Unknown ancillary data from mechanism");
  }

  if (serial_port_fd < 0) {
    fprintf(stderr, "Error: %s\n", "Did not get a file descriptor from the mechanism");
    exit(EX_PROTOCOL);
  }

  shutdown(unix_peer_fd, SHUT_RDWR);
  close(unix_peer_fd);

  /* USE THE SERIAL PORT CODE */

  const char serial_message[] = "Hello World\r\n";
  ssize = TEMP_FAILURE_RETRY(
    write(serial_port_fd, serial_message, sizeof(serial_message))
  );
  if (ssize != sizeof(serial_message)) {
    fprintf(stderr, "Error: %s\n", "Could not complete message to serial port");
    exit(EX_IOERR);
  }

  exit(EXIT_SUCCESS);

}
