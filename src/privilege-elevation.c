#include <stdlib.h>     // EXIT_FAILURE, exit(), mkdtemp(), getenv(), atexit()
#include <stdio.h>      // printf(), snprintf(), remove()
#include <stddef.h>     // NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // size_t, strcmp(), strcpy()
#include <unistd.h>     // read(), write(), fork(), exec(), getpid(), getppid()
#include <errno.h>      // perror()
#include <sysexits.h>   // EX_USAGE, EX_CANTCREAT, EX_UNAVAILABLE
#include <ftw.h>        // nftw()
#include <libgen.h>     // basename()
#include <assert.h>     // assert()
#include <fcntl.h>      // fcntl()
#include <sys/socket.h> // PF_UNIX, SOCK_STREAM, CMSG_SPACE, socklen_t, struct ucred, struct msghdr, socket(), bind(), listen(), accept(), getsockopt
#include <sys/un.h>     // UNIX_PATH_MAX, struct sockaddr_un
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid()
#include <sys/select.h> // pselect()
#include <sys/prctl.h>  // PR_SET_PDEATHSIG, prctl()
#include <signal.h>     // SIGCHLD, SIGTERM, sigset_t, struct sigaction, sigemptyset(), sigaddset(), sigprocmask(), sigaction()

#include "argparse.h"
#include "protocol.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#if !defined(PKEXEC_PATH) || !defined(MECHANISM_PATH)
    #error "PKEXEC_PATH and MECHANISM_PATH must be defined."
#endif

static char unix_sock_dir[UNIX_PATH_MAX] = {0};

static int unix_sock_fd = 0;
static int unix_peer_fd = 0;

static volatile sig_atomic_t mechanism_status= -1;

static int
ntfw_callback (const char * path, const struct stat * sb, int type_flag, struct FTW * ftw_buf) {

    int status = remove(path);

    if (status != 0) {
        char error_string[8 + UNIX_PATH_MAX];
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
        ntfw(unix_sock_dir, ntfw_callback, 64, FTW_DEPTH | FTW_PHYS);
    }

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

  return (sigprocmask(SIG_SET, signal_orig_mask, NULL) == 0);

}

/**
 * Assigns a signal handler to SIGCHLD.
 * This should run after SIGCHLD is first blocked.
 */
static int
handle_sigchld (void (*handler)(void), int flags) {

  struct sigaction signal_action = {0};
  signal_action.sa_flags = flags;
  signal_action.sa_sigaction = handler;
  return (sigaction(SIGCHLD, &signal_action, NULL) == 0);

}

/**
 * Handles the SIGCHLD from the mechanism process.
 */
static void
handle_mechanism_process (int signal, siginfo_t * signal_info, void * context) {

  assert(signal == SIGCHLD);

  if (signal_info->si_code == CLD_EXITED) {
    mechanism_status = signal_info->si_status;
  }

}

static int
check_peer_pid (int peer_sock_fd, int peer_pid) {

  struct ucred peer_credentials = {0};
  int peer_credentials_size = sizeof(peer_credentials);
  if (
      getsockopt(
                 peer_sock_fd,
                 SOL_SOCKET,
                 SO_PEERCRED,
                 &peer_credentials,
                 &peer_credentials_size
                 )
      != 0
  ) {
    return 0;
  }

  return (peer_credentials.pid == peer_pid);

}

static int
exec_mechanism (char const * process_path, char const * process_arguments[]) {

  // setup a pipe for between parent and forked child process
  // to communicate errors during the fork prior to the exec
  int exec_pipe[2];
  if (pipe(exec_pipe) != 0) {
    return -1;
  }

  pid_t parent_pid = getpid();
  pid_t mechanism_pid = fork();

  if (mechanism_pid == -1) {

    perror("fork()");

  } else if (mechanism_pid == 0) {

    // close the read side in the child
    close(exec_pipe[0]);

    // if the parent dies, we want the child to commit suicide
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
      // if the parent had died, this would result in a SIGPIPE
      // which close the parent process as well
      write(exec_pipe[1], &errno, sizeof(errno));
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
      write(exec_pipe[1], &errno, sizeof(errno));
      exit(EX_OSERR);
    }

    // execute the process with the arguments
    // the child process can access file descriptors in the parent
    // however we need to pass file descriptors from the child to the parent
    // so we'll be using unix domain sockets for communication
    execv(process_path, process_arguments);

    // exec failed, we must write the errno into the pipe
    write(exec_pipe[1], &errno, sizeof(errno));
    // exit the child process
    exit(EX_UNAVAILABLE);

  }

  if (mechanism_pid == -1) {
    return -1;
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
    return -1;
  }

  // exec succeeded, close the read end
  close(exec_pipe[0]);

  return mechanism_pid;

}

static bool
parse_args (
  int argc,
  char * * argv,
  char * * argv_,
  uint32_t * baud,
  char * * serial_port
) {

  memcpy(argv_, argv, sizeof(char *) * argc);

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

  argparse_describe(&argparse, "\nThis demonstrates lazy privilege elevation via opening a secured serial port resource.");

  int argc_ = argparse_parse(&argparse, argc, argv_);

  if (argc_ < 1) {
    argparse_usage(&argparse);
    return false;
  }

  *serial_port = argv_[0];

  return true;

}

static int
setup_unix_sock (const char * sock_path, int backlog, bool nonblocking) {

  struct sockaddr_un unix_sock_addr = {
    .sun_family = AF_UNIX,
    .sun_path = sock_path
  };

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
launch_mechanism (
  const char * mechanism_path,
  const char * pkexec_path,
  char * * mechanism_args,
  int sock_fd,
  sigset_t * signal_orig_mask,
  bool privileged
) {

  // we need to create our own array given the input array
  // it requires mallocing, due to dynamic ness of the array

  if (privileged) {
    // change the mechanism path
    // and the mechanism name
  }

  int mechanism_pid = exec_mechanism(mechanism_path, mechanism_args);

  if (mechanism_pid == -1) {
    return -1;
  }

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(sock_fd, &readfds);

  while (1) {

    int status = pselect(1, &readfds, NULL, NULL, NULL, signal_orig_mask);
    if (status == -1) {

      if (errno != EINTR) {
        return -1;
      }

      if (mechanism_status == EX_NOPERM) {
        return launch_mechanism(mechanism_path, pkexec_path, sock_fd, signal_orig_mask, true);
      } else {
        return -1;
      }

      // if not interrupted from the mechanism, just continue

    } else if (status > 0 && FD_ISSET(unix_sock_fd, &readfds)) {

      break;

    }

  }

  return mechanism_pid;

}

int
main (int argc, char * * argv) {

  /* SETUP ENVIRONMENT */

  atexit(cleanup_and_exit);

  const char * tmp_dir= getenv("TMPDIR");
  if (!tmp_dir) tmp_dir = "/tmp";
  const char tmp_name[] = "polkit_demo.XXXXXX";
  const char socket_name[] = "socket.sock";

  char pkexec_path[] = XSTR(PKEXEC_PATH);
  char mechanism_path[] = XSTR(MECHANISM_PATH);

  char pkexec_name[] = XSTR(PKEXEC_PATH);
  basename(pkexec_name);
  char mechanism_name[] = XSTR(MECHANISM_PATH);
  basename(mechanism_name);

  int serial_port_fd = -1;

  uint32_t baud = 0;
  char * serial_port;

  char * * argv_ = malloc(sizeof(char *) * argc);
  if (!parse_args(argc, argv, argv_, &baud, &serial_port)) {
    exit(EX_USAGE);
  }

  if (!baud) {
    baud = 9600;
  }

  assert(UNIX_PATH_MAX <= (
    sizeof(temporary_folder) +
    sizeof(temporary_namespace) +
    sizeof(socket_name) + 1)
  );

  char template[sizeof(tmp_dir_size) + sizeof(tmp_name_size)];
  snprintf(template, sizeof(template), "%s/%s", tmp_dir, tmp_name);
  unix_sock_dir = mkdtemp(template);

  if (!unix_sock_dir) {
    perror("create_tmp_namespace()");.
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
  unix_sock_fd = setup_unix_socket(unix_sock_path, 1, true);
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
  if (!handle_sigchld(handle_mechanism_process, SA_SIGINFO | SA_NOCLDSTOP)) {
    perror("handle_sigchld()");
    exit(EX_OSERR);
  }

  /* UNPRIVILEGED EXECUTION CODE */

  char * mechanism_args[] = {
    mechanism_name,
    serial_port,
    unix_sock_path,
    (char *) NULL
  };

  char * privileged_mechanism_args[] = {
    pkexec_name,
    mechanism_path,
    serial_port,
    unix_sock_path,
    (char *) NULL
  };





  int mechanism_pid = launch_mechanism(mechanism_path, mechanism_args);

  if (mechanism_pid == -1) {
    perror("launch_mechanism()");
    exit(EX_OSERR);
  }

  // now we have 2 ways of receiving information about the mechanism
  // if the mechanism is broken, we'll receive information via SIGCHLD
  // if the mechanism works, we'll receive information via the socket

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(unix_sock_fd, &readfds);

  while (1) {

    int status = pselect(1, &readfds, NULL, NULL, NULL, &signal_orig_mask);
    if (status == -1) {

      if (errno != EINTR) {
        perror('pselect()');
        exit(EX_OSERR);
      }

      if (mechanism_status == EX_NOPERM) {
        // restart!

      } else {
        perror('handle_mechanism_process()');
        exit(EX_UNAVAILABLE);
      }

      // if not interrupted from the mechanism, just continue

    } else if (status > 0 && FD_ISSET(unix_sock_fd, &readfds)) {

      break;

    }

  }

  // if this is reached, there is a pending file descriptor
  // we can restart if the process failed

  // permanently unmask the SIGCHLD, allowing it to interrupt us
  if (!unblock_sigchld(&signal_orig_mask)) {
    perror("unblock_sigchld()");
    exit(EX_OSERR);
  }

  struct sockaddr_un unix_peer_addr = {0};
  socklen_t unix_peer_addr_size = sizeof(unix_peer_addr);

  unix_peer_fd = accept(
    unix_sock_fd,
    (struct sockaddr *) &unix_peer_addr,
    &unix_peer_addr_size
  );

  if (unix_peer_fd == -1) {

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      perror("accept()");
      exit(EX_UNAVAILABLE);
    } else if (errno == EINTR && error_from_mechanism_signal) {
      perror('handle_mechanism_process()');
      exit(EX_UNAVAILABLE);
    } else {
      perror('accept()');
      exit(EX_OSERR);
    }

  }

  if (!check_peer_pid(unix_peer_fd, mechanism_pid)) {
    perror("check_peer_pid()");
    exit(EX_PROTOCOL);
  }

  shutdown(unix_peer_fd, SHUT_WR);

  /* UNPRIVILEGED RECEIVE CODE */

  // the accepted connection is not non-blocking

  char message_buffer[sizeof(MechanismProto)] = {0};

  struct iovec io_vector[1] = {
    {
      .iov_base = message_buffer,
      .iov_len = sizeof(message_buffer)
    }
  };

  // buffer for the ancillary data (the file descriptor)
  char ancillary_buffer[CMSG_SPACE(sizeof(int))] = {0};

  // msghdr is the socket options for sendmsg and recvmsg
  struct msghdr message_options = {0};
  message_options.msg_iov = io_vector;
  message_options.msg_iovlength = sizeof(io_vector);
  message_options.msg_control = ancillary_buffer;
  message_options.msg_controllen = sizeof(ancillary_buffer);

    // we will spin on receiving data from the mechanism, but sleep 1
    // also we shall set that the received file descriptor needs to be closed
    // if the main process execs (this is for security reasons)
    do {

      ssize_t read_size = recvmsg(unix_peer_fd, &message_options, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);

      if (read_size <= 0 && error_from_mechanism_signal) {
        perror('handle_mechanism_process()');
        exit(EX_UNAVAILABLE);
      } else if (read_size < -1) {
        perror("recvmsg()");
        exit(EX_OSERR);
      }

      sleep(1);

    } while (read_size < 1);

    // our protocol is just 1 byte
    // however if it were bigger, we would need to collect up the bytes into a larger message frame
    // it would work like this, used a linked list of reads
    // reads can be of arbitrary size, but here we'll limit it to size of 1 or more
    // then after each read, you drive the parser
    // the parser maintains a pointer to the head of the linked list, while also a pointer within a head
    // as it parses a single message, it would deallocate n-heads of the linked list, but not within a head (it will just use the offset)
    // once it has 1 atomic message (a parse result), it can yield that result, and call something else
    // in fact it's a composition, we can have a parser for a single atomic message, and then a higher level context free parser that parses a sequence of atomic messages to form an expression
    // in this case of a struct, we have 2 levels, one for parsing a single atomic struct field, but the higher level understands the entire struct
    // this in essence unpacks the buffer into a struct
    // a struct has padding and endianness, whereas the network protocol itself is packed and has a defined endianness as well

    MechanismProto message = (MechanismProto) (message_buffer[0]);

    if (message.type == PERMERR) {

      // we need to restart with privileged access
      // using polkit to launch mechanism
      // we need to abstract all of this into a recursive function that retries
      // this means launching a mechanism again, with the same socket
      // accepting a connection
      // reading from the connection

      // how to rearchitect this?
      // it needs to be like a retry monad
      // a function that recalls itself once when it fails

    } else if (message.type == PRIVFD) {

      // we have the file descriptor, let's get it
      if ((message_options.msg_flags & MSG_CTRUNC) == MSG_CTRUNC) {
        fprintf(stderr, "Not enough space provided for ancillary element array");
        exit(EX_SOFTWARE);
      }

      // don't we only expect 1 message
      // why would we iterate over control messages?

      // it's possible that the sender may send multiple control messsages for a single data

      for (struct cmsghdr * control_message = CMSG_FIRSTHDR(&message_options);
           control_message != NULL;
           control_message = CMSG_NXTHDR(&message_options, control_message))
        {
          if (
              (control_message->cmsg_level == SOL_SOCKET)
              &&
              control_message->cmsg_type == SCM_RIGHTS
              )
            {
              serial_port_fd = *((int *) CMSG_DATA(control_message));
              break;
            }
        }

      if (serial_port_fd < 0) {
        fprintf(stderr, "Did not get a file descriptor from the mechanism");
        exit(EX_SOFTWARE);
      }

      received_from_mechanism_socket = 1;

      // is this necessary?
      shutdown(unix_peer_fd, SHUT_RDWR);
      close(unix_peer_fd);

      shutdown(unix_sock_fd, SHUT_RDWR);
      close(unix_sock_fd);

      // try out the serial_port_fd
      // read from it, and write to it!
      // whatever!!

    }

    //http://www.techdeviancy.com/uds.html


}
