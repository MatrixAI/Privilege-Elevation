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

static volatile sig_atomic_t received_from_mechanism_socket = 0;
static volatile sig_atomic_t error_from_mechanism_signal = 0;

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

static void cleanup_and_exit () {

    if (unix_peer_fd) close(unix_peer_fd);
    if (unix_sock_fd) close(unix_sock_fd);
    if (unix_sock_dir && *unix_sock_dir) {
        ntfw(unix_sock_dir, ntfw_callback, 64, FTW_DEPTH | FTW_PHYS);
    }

}

static void
handle_mechanism_process (int signal, siginfo_t * signal_info, void * context) {

  assert(signal == SIGCHLD);

  if (!received_from_mechanism) {
    error_from_mechanism_signal = 1;
  } else if (signal_info->si_code != CLD_EXITED || signal_info->si_status != EXIT_SUCCESS) {
    error_from_mechanism_signal = 1;
  }

  // otherwise a normal exit

}

int
launch_mechanism (char const * process_path, char const * process_arguments[], int exec_pipe[2]) {

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

  return mechanism_pid;

}

int
main (int argc, char * * argv) {

    atexit(cleanup_and_exit);

    static const char * const command_usage[] = {
        "privilege-elevation [options] [--] <serial-port-path>",
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
    argparse_describe(&argparse, "\nThis demonstrates lazy privilege elevation via opening a secured serial port resource.");

    // make sure the original argc and argv is preserved
    const char * * argv_ = malloc(sizeof(char *) * argc);
    memcpy(argv_, argv, sizeof(char *) * argc);

    int argc_ = argparse_parse(&argparse, argc, argv_);

    if (argc_ < 1) {
        argparse_usage(&argparse);
        exit(EX_USAGE);
    }

    // both the selected_baud and serial_port will be passed to open-serial-device
    const char * serial_port = argv_[0];

    // unix domain socket name
    const char socket_name[] = "socket.sock";

    // default temporary folder
    const char * temporary_folder = getenv("TMPDIR");
    if (!temporary_folder) {
        temporary_folder = "/tmp";
    }

    // template for generating the temporary directory
    char template[UNIX_PATH_MAX - sizeof(socket_name) + 1]; // +1 for \0 byte
    if (snprintf(
            template,
            sizeof(template),
            "%s/%s",
            temporary_folder,
            "polkit_demo.XXXXXX"
        ) >= sizeof(template)
    ) {
        fprintf(stderr, "$TMPDIR path for saving the socket is too long.\n");
        exit(EX_USAGE);
    }

    // creating a temporary directory
    char * directory = mkdtemp(template);
    if (directory == NULL) {
        perror("mkdtemp()");.
        exit(EX_CANTCREAT);
    }

    // copy temporary directory to static variable, so that it can be cleaned up on exit
    strcpy(unix_sock_dir, directory);

    // the unix_sock_path = unix_sock_dir + socket_name
    char unix_sock_path[UNIX_PATH_MAX];
    snprintf(unix_sock_path, sizeof(unix_sock_path), "%s/%s", unix_sock_dir, socket_name);

    // our unix socket address
    struct sockaddr_un unix_sock_addr = {
        .sun_family = AF_UNIX,
        .sun_path = unix_sock_path
    };

    // initialise a new socket
    unix_sock_fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (unix_sock_fd < 0) {
        perror("socket()");
        exit(EX_OSERR);
    }

    // bind the socket to the socket address
    if (bind(unix_sock_fd, (struct sockaddr *) &unix_sock_addr, sizeof(unix_sock_addr)) != 0) {
        perror("bind()");
        exit(EX_OSERR);
    }

    // listen to the socket address
    // we only expect 1 client, so a backlog of 1 is fine
    if (listen(unix_sock_fd, 1) != 0) {
        perror("listen()");
        exit(EX_OSERR);
    }

    // set the listening socket to non-blocking
    // this allows us to use select to multiplex checking for child process termination or connection
    if (fcntl(unix_sock_fd, F_SETFL, (fcntl(unix_sock_fd, F_GETFL, 0) | O_NONBLOCK)) == -1) {
        perror("fcntl()");
        exit(EX_OSERR);
    }

    // setup the executable paths
    // the paths will be used for execution, while the names will be used for process names
    // basename mutates its parameter, so we need to duplciate it
    char pkexec_path[] = XSTR(PKEXEC_PATH);
    char pkexec_name[] = XSTR(PKEXEC_PATH);
    pkexec_name = basename(pkexec_name);
    char mechanism_path[] = XSTR(MECHANISM_PATH);
    char mechanism_name[] = XSTR(MECHANISM_PATH);
    mechanism_name = basename(mechanism_name);

    // block SIGCHLD in addition to any existing signals being blocked
    sigset_t signal_sigchld_mask;
    sigset_t signal_current_mask;
    sigemptyset(&signal_sigchld_mask);
    sigaddset(&signal_sigchld_mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &signal_sigchld_mask, &signal_current_mask) != 0) {
        perror("sigprocmask()");
        exit(EX_OSERR);
    }

    // setup the signal handler for SIGCHLD prior to masking SIGCHLD and spawning the child process
    // SIGCHLD carries the information about the child process, to make use of it we need to use SA_SIGINFO flag
    // also we don't care if the child process is suspended and continued, so we also add SA_NOCLDSTOP flag
    struct sigaction signal_action = {0};
    signal_action.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;
    signal_action.sa_sigaction = handle_mechanism_process;
    if (sigaction(SIGCHLD, &signal_action, NULL) == -1) {
        perror('sigaction()');
        exit(EX_OSERR);
    }

    // setup a pipe for between parent and forked child process
    // to communicate errors during the fork prior to the exec
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
      perror("pipe()");
      exit(EX_OSERR);
    }

    // attempt unprivileged open-serial-device
    char * mechanism_args[] = {
      mechanisms_name,
      serial_port,
      unix_sock_path,
      (char *) NULL
    };

    int mechanism_pid = launch_mechanism(mechanism_path, mechanism_args, exec_pipe);

    if (mechanism_pid == -1) {
      perror("launch_mechanism()");
      exit(EX_OSERR);
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
        fprintf(stderr, "Could not execute the mechanism.\n");
        fprintf(stderr, "execl(): %s\n", strerror(exec_errno));
        exit(EX_UNAVAILABLE);
    }

    // exec succeeded, close the read end
    close(exec_pipe[0]);

    // now we have 2 ways of receiving information about the mechanism
    // if the mechanism is broken, we'll receive information via SIGCHLD
    // if the mechanism works, we'll receive information via the socket

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(unix_sock_fd, &readfds);

    while (1) {

      int status = pselect(1, &readfds, NULL, NULL, NULL, &signal_current_mask);

      if (status == -1) {
        if (errno == EINTR) {
          if (error_from_mechanism_signal) {
            perror('handle_mechanism_process()');
            exit(EX_UNAVAILABLE);
          }
        } else {
          perror('pselect()');
          exit(EX_OSERR);
        }
      } else if (status > 0) {
        // break if we have pending connection on the unix sock fd
        if (FD_ISSET(unix_sock_fd, &readfds)) {
          break;
        }
      }

    }

    // permanently unmask the SIGCHLD, allowing it to interrupt
    // at this point we can check for the 
    if (sigprocmask(SIG_SET, &signal_current_mask, NULL) != 0) {
        perror("sigprocmask()");
        exit(EX_OSERR);
    }

    struct sockaddr_un unix_peer_addr = {0};
    socklen_t unix_peer_addr_size = sizeof(unix_peer_addr);

    unix_peer_fd = accept(unix_sock_fd, (struct sockaddr *) &unix_peer_addr, &unix_peer_addr_size);

    // if this fails, accept failed even though pselect told us there was a pending connection
    // this is just abnormal error, we just fail here
    // in other cases, a supervisor may recover from this error
    if (unix_peer_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        perror("accept()");
        exit(EX_UNAVAILABLE);
      } else {
        if (errno == EINTR) {
          if (error_from_mechanism_signal) {
            perror('handle_mechanism_process()');
            exit(EX_UNAVAILABLE);
          }
        } else {
          perror('accept()');
          exit(EX_OSERR);
        }
      }
    }

    // accepted a connection
    // we still need the socket in case we need to elevate permissions

    // check if the peer is the PID that we intended to launch
    struct ucred peer_credentials = {0};
    int peer_credentials_size = sizeof(peer_credentials);

    if (getsockopt(unix_peer_fd, SOL_SOCKET, SO_PEERCRED, &peer_credentials, &peer_credentials_size) != 0) {
      perror("getsockopt()");
      exit(EX_OSERR);
    }

    // I don't think this works with datagram sockets, datagram sockets are unlikely to have any kind of peer credentials
    // our message is so small, it should fit atomically into 1 byte
    // then we don't need to worry about partial sends or partial writes
    // and we can keep using peercred to ensure that the correct child process is the one sending data!!
    // woo!
    // even then, we STILL USE sendmsg and recvmsg
    // and we can use pselect as well...
    // since we still need to deal with signal failure in multiple areas
    if (peer_credentials.pid != mechanism_pid) {
      printf("The connecting peer's PID did not match the launched mechanism PID\n");
      exit(EX_PROTOCOL);
    }

    // file descriptors are passed as ancillary data
    // so we just a typed message indicating whether we have ancillary data
    // or that we lost permissions to acquire the data

    // how do we combine the unix fd message type with our own type?
    // is unix_peer_fd an actual Unix Domain Socket now?
    // or it still something else?

    // so we'll use the sendmsg and recvmsg pair
    // these 2 allow us to setup a msghdr (really just a message)
    // that will contain both the custom type we have, along with the ancillary data

    MechanismProto message_buffer[1] = {0};

    // this will store the actual message
    struct iovec io_vector[1] = {
      {
        .iov_base = message_buffer,
        .iov_len = sizeof(message_buffer)
      }
    };

    // buffer for the ancillary data (the file descriptor)
    char ancillary_buffer[CMSG_SPACE(sizeof(int))] = {0};

    // a msghdr is really just the socket options, options for the sendmsg and recvmsg
    struct msghdr message_options = {0};
    message_options.msg_iov = io_vector;
    message_options.msg_iovlength = sizeof(io_vector);
    message_options.msg_control = ancillary_buffer;
    message_options.msg_controllen = sizeof(ancillary_buffer);

    struct cmsghdr * control_message = NULL;

    

    // in a SOCK_DGRAM
    // it is connection less
    // so there is no connection to listen to
    // you just bind to the socket, and start reading or sending data
    // in the same way, there's no connect on the client side
    // it also needs to bind to the same socket
    // so we need to remove the listen part
    // because a SOCK_DGRAM is more suited to this type of network architecture
    // we can still use the same recvmessage
    // we are just relying on C to typecast the received message as the MechanismProto
    // and we are done!
    // datagram sockets don't have accept and connect semantics
    // we only have bind, recvmsg and sendmsg semantics right...
    // OOOOHHHH
    // however because the socket is nonblocking, we still need to use pselect right to acquire the recvmsg?

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(unix_sock_fd, &readfds);

    int status = pselect(1, &readfds, NULL, NULL, NULL, &signal_current_mask);

    if (status == -1) {

    }



    // i still have a signal mask
    // i need to allow that to occur (so I can catch the possibly of the mechanism failing)
    // and use pselect on the socket to receive the actual message
    if (recvmsg (unix_peer_fd, &socket_message, MSG_CMSG_CLOEXEC) < 0) {
      // fail?
    }

    // socket_message contains the io_vector which contains the message buffer
    // what does this mean?
    // a char is 1 byte
    // our message buffer is an array of 1 byte!?
    // why are we use a message header?


    // both the server and the client includes this header as a shared protocol
    // the server now performs a pselect (while unmasking to get the signal as well)
    // on the connected socket, and then uses recvmsg to acquire the message
    // and it needs to switch on the 2 different messages
    // apparently the client program also in order to connect to our unix domain socket
    // it must create its own unix domain socket
    // in the same temporary directory
    // it will then bind to it (creating the file), and then connect it to the server socket
    // the client program should be able to unlink the socket file as soon as it has binded to it
    // the server program should be able to unlink the its socket file as soon as it received a connection
    // ... however, the problem is that we may expecting a privileged invocation
    // this means we may need to reuse the socket, so we cannot delete or even close the socket
    // until we are sure that we have the file descriptor, or we are exiting out of the program completely
    //http://www.techdeviancy.com/uds.html

    // now receive the file descriptor from the mechanism
    // or an error message indicating permission error
    // if we receive a permission error, we must retry with elevated permissions

    // use MechanismProto

}
