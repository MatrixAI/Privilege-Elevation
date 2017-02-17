#include <stdlib.h>     // EXIT_FAILURE, exit(), mkdtemp(), getenv(), atexit()
#include <stdio.h>      // printf(), snprintf(), remove()
#include <stddef.h>     // NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // size_t, strcmp(), strcpy()
#include <unistd.h>     // read(), write(), fork(), exec()
#include <errno.h>      // perror()
#include <sysexits.h>   // EX_USAGE, EX_CANTCREAT, EX_UNAVAILABLE
#include <ftw.h>        // nftw()
#include <libgen.h>     // basename()
#include <assert.h>     // assert()
#include <sys/socket.h> // PF_UNIX, SOCK_STREAM, socklen_t, struct ucred, socket(), bind(), listen(), accept(), getsockopt
#include <sys/un.h>     // UNIX_PATH_MAX, struct sockaddr_un
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid()
#include <sys/select.h> // pselect()
#include <signal.h>     // sigset_t, struct sigaction, sigemptyset(), sigaddset(), sigprocmask(), sigaction()
#include "argparse.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#if !defined(PKEXEC_PATH) || !defined(MECHANISM_PATH)
    #error "PKEXEC_PATH and MECHANISM_PATH must be defined."
#endif

static char unix_sock_dir[UNIX_PATH_MAX] = {0};

static int ntfw_callback(const char * path, const struct stat * sb, int type_flag, struct FTW * ftw_buf) {

    int status = remove(path);

    if (status != 0) {
        char error_string[8 + UNIX_PATH_MAX];
        snprintf(error_string, sizeof(error_string), "remove(%s)", path);
        perror(error_string);
    }

    return status;

}

static void cleanup_and_exit() {

    if (unix_sock_dir && *unix_sock_dir) {

        ntfw(unix_sock_dir, ntfw_callback, 64, FTW_DEPTH | FTW_PHYS);

    }

}

static void handle_mechanism_process (int signal, siginfo_t * signal_info, void * context) {

    // the signal_info properties...
    /* signal_info->si_pid, */
    /* signal_info->si_uid, */
    /* signal_info->si_status, */
    /* signal_info->si_utime, */
    /* signal_info->si_stime */

    // the only signal we should be handling here is a SIGCHLD
    assert(signal == SIGCHLD);

    switch (signal_info->si_code) {
        case CLD_EXITED:
            switch (signal_info->si_status) {
                case EX_NOPERM:
                    // so basically this means we retry with pkexec
                    // how should we communicate back to the main thread?
                    // a volatile variable?
                break;
            }
        break;
        case CLD_KILLED:
        case CLD_DUMPED:
            // child was killed, this is abnormal, should exit parent process somehow
            // can we exit the main thread here, or is it better to propagate to the parent some how?
            // exit is not a async signal safe function, which means it's not atomic
            // while running the exit, another signal may be received, and handling that will interrupt the exit call
            // the exit call will call the atexit functions, while _exit which is a signal safe function won't it will just close immediately
            // an extra signal being emitted here could mean that the same signal is received, like another SIGCHLD, this causes the same signal handler to run again
            // when dealing with non-atomic actions, the behaviour of the program becomes undefined
            // so how to solve this?
            // the only way to make this manageable is to queue up the signals, and handle them with a event loop/state machine
            // on linux, we could use signalfd
            // to be cross-platform, we could use libuv
            // since we are only using polkit, then signalfd could be way forward
            // the other thing is that you could check whether you're handling a signal or not for every signal handler, but thist would require applying it to all signals
            // also our exit handler should be used for normal exits, and for when a signal is received
            // ultimately this all still requires an event-driven mindset
            // currently all the signal events are asynchronous, causing problems for programming, instead we need to leave signals as asynchronous, but the handling of such signals to be synchronous
            // note that from the kernel's perspective all software interrupts from the userspace (syscalls) are synchronous, because while handling a syscall, no other process could be executing a syscall (single-threaded that is)
            // but the kernel needs to deal with hardware interrupts in the same way
            // ok so i guess the best solution to read up on signalfd here because this program only cares about Linux...
            // remember to close signal fd (unmask it) before calling child process, because only the parent process should be handling the signal fd
            // the caller can decide how to send the signal to the child process
            // but in this case, I don't want to rely on the caller, or process groups
            // simply that if the parent dies, the child must also die
            // so there is a way to propagate the death signals to the child as well
          exit(EX_UNAVAILABLE);
        break;
    }

}

int main(int argc, char * * argv) {

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
    int unix_sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (unix_sock_fd < 0) {
        perror("socket()");
        exit(EX_OSERR);
    }

    // set the listening socket to non-blocking
    // because: http://stackoverflow.com/a/3444832/582917
    if (fcntl(unix_sock_fd, F_SETFL, (fcntl(unix_sock_fd, F_GETFL, 0) | O_NONBLOCK)) == -1) {
        perror("fcntl()");
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

    // setup the execution paths
    // basename mutates its parameter, so we need to duplciate it
    char pkexec_path[] = XSTR(PKEXEC_PATH);
    char pkexec_name[] = XSTR(PKEXEC_PATH);
    pkexec_name = basename(pkexec_name);
    char mechanisms_path[] = XSTR(MECHANISM_PATH);
    char mechanisms_name[] = "open-serial-device";

    // setup a pipe for between parent and forked child process
    // to communicate errors during the fork prior to the exec
    int exec_errno_pipefd[2];
    pipe(exec_errno_pipefd);

    // block SIGCHLD in addition to any existing signals being blocked
    sigset_t signal_sigchld_mask;
    sigset_t signal_current_mask;
    sigemptyset(&signal_sigchld_mask);
    sigaddset(&signal_sigchld_mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &signal_sigchld_mask, &signal_current_mask) != -1) {
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

    // attempt unprivileged open-serial-device
    // if we already have the permissions, this is enough to open the serial device
    // however if we lack the permissions, then we will attempt privilege elevatation with pkexec
    pid_t mechanism_pid = fork();

    if (mechanism_pid == -1) {

        perror("fork()");
        exit(EX_OSERR);

    } else if (mechanism_pid == 0) {

        // child is only writing
        close(exec_errno_pipefd[0]);

        // set close-on-exec flag using fcntl on the writing end
        // this means if execl succeeds, the pipe will be closed
        // signaling to the parent that it succeeded executing the mechanism
        // however this does not guarantee that the mechanism actually succeeds
        fcntl(exec_errno_pipefd[1], F_SETFD, FD_CLOEXEC);


        // it's possible for the execed child process to access the file descriptor
        // as long as you progagate the child descriptor number to the execed child process
        // it can use fdopen(...)
        // this allows the execed process and not just the forked process to talk back to the parent process
        // however, while this can be used for child -> parent communication
        // and parent -> child communication
        // this requires the parent to establish 3 pipefds and redirect the stdin and stdout and potentially stderr to the pipefds
        // regardless the execed process will inherit the parent process's stdin, stdout, and stderr for all file descriptors

        // try to first execute it in an unprivileged way
        // open-serial-device <serial-port-path> <unix-domain-socket-path>
        execl(mechanisms_path, mechanisms_name, serial_port, unix_sock_path, (char *) NULL);

        // if we get here, execl failed, we can write the errno for execl failure
        // errno is defined to be of size int, we can write each byte into the pipe
        write(exec_errno_pipefd[1], &errno, sizeof(errno));
        exit(EX_UNAVAILABLE);

    }

    // parent is only reading
    close(exec_errno_pipefd[1]);

    int exec_errno;
    if (read(exec_errno_pipefd[0], &exec_errno, sizeof(exec_errno)) > 0) {

        // the mechanism was never executed
        fprintf(stderr, "Could not execute the mechanism.\n");
        fprintf(stderr, "execl(): %s\n", strerror(exec_errno));
        close(exec_errno_pipefd[0]);
        exit(EX_UNAVAILABLE);

    }

    // execl succeeded, the pipe is no longer needed
    close(exec_errno_pipefd[0]);

    // the mechanism has been execed
    // now need to listen for 2 things:
    // 1. a SIGCHLD signal for if the child process exits without writing the unix domain socket
    // 2. a message from the unix domain socket about what happened
    // either the opening succeeded or it failed
    // if it failed, it could fail due to no permissions available to read
    // if that happens, an exit code should be used to communicate this, and SIGCHLD needs to be handled
    // if it succeeded, the file descriptor should be passed via the unix domain socket
    // otherwise a SIGCHLD with something else could indicate a different error!
    // so actually 3 conditions:
    // SIGCHLD
    // unix file descriptor value (and then SIGCHLD)
    // once we receive the file descriptor, the child process can exit, and we don't really care about it
    // we have got what we want!


    // the best solution appears to be pselect
    // it appears to be an alternative to the self-pipe trick
    // however pselect is not as portable as the self-pipe trick
    // this article explains the main problem: https://lwn.net/Articles/176911/

    // mechanism's unix socket address
    struct sockaddr_un unix_peer_addr = {0};
    socklen_t unix_peer_addr_size = sizeof(unix_peer_addr);

    // this is the socket we have to listen on...
    // accept(unix_sock_fd (struct sockaddr *) &unix_peer_addr, &unix_peer_addr_size) != -1
    // at the same time if we receive other signals, we must propagate these to these to the children?
    // pselect will block until the unix domain socket has pending or until it is interrupted by a signal
    // the signal we are looking for is SIGCHLD, but other signals could interrupt it as well
    // here we do not have a timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(unix_sock_fd, &readfds);

    // unmask SIGCHLD, allowing it to be handled during the pselect call
    // pselect works here because we jump into kernel-space, resolving the race-condition between a sigprocmask and select
    if (pselect(1, &readfds, NULL, NULL, NULL, &signal_current_mask) == -1) {
        perror('pselect()');
        exit(EX_OSERR);
    }

    // permanently unmask the SIGCHLD, allowing it to be handled
    if (sigprocmask(SIG_SET, &signal_current_mask, NULL) != -1) {
        perror("sigprocmask()");
        // if we exit here, how do we propagate the exit to the child process?
        exit(EX_OSERR);
    }

    // so this means either we got a connection attempt from the unix domain socket
    // or that we got interrupted by SIGCHLD, or another signal
    // if SIGCHLD, we need to deal with this
    // if another signal, do we just exit here?

    // also since the listening socket is non-blocking, we can accept the connection
    // but the client may have disconnected prior to doing so, if the client disconnects
    // we should also exit, because the client shouldn't be doing such a thing!



    int mechanism_status;
    while (waitpid(mechanism_pid, &mechanism_status, WNOHANG) == 0 && accept(unix_sock_fd, (struct sockaddr *) &unix_peer_addr, &unix_peer_addr_size) == -1) {
        sleep(1);
    }


    // I DON'T like the above solution...
    // An alternative is: http://stackoverflow.com/a/29245438/582917
    // another alternative is to use pselect: http://stackoverflow.com/a/29245576/582917
    // Basically we need a SIGCHLD signal handler
    // The signal handler should write to another pipe
    // Then a select can select on both this pipe and the unix domain socket file descriptor
    // Anyway, SIGCHLD will be received by the parent, when the child's process status changes
    // HOWEVER, how does interact in the case where execl() simply failed?
    // Wouldn't SIGCHLD also be initiated if execl failed?
    // If we do this and SIGCHLD does activate, we must check that the status IS not of one the process status
    // OR another way would be to just rely on SIGCHLD pipe, and then differentiate the exit status codes between execl failure and mechanism process failure

    // The problem is this:
    // We need to handle 3 return types.
    // 1. When the execl failed - just exit
    // 2. When the execl succeeded by the process failed - handle the process failure
    // 3. When the execl succeeded, the process succeeded is attempting to connect to the unix domain socket - accept the connection
    // I feel like all 3 should be some sort of file descriptor to select on

    // accept the connection from the child process
    // we only expect 1 connection, so there's no forking the connection handler
    int unix_peer_fd = accept(unix_sock_fd, (struct sockaddr *) &unix_peer_addr, &unix_peer_addr_size);

    if (unix_peer_fd == -1) {
        perror("accept()");
        exit(EXIT_FAILURE);
    }

    close(unix_sock_fd);

    // check if the peer is the PID that we intended to launch

    struct ucred peer_credentials;
    int peer_credentials_size = sizeof(peer_credentials);

    if (getsockopt(unix_peer_fd, SOL_SOCKET, SO_PEERCRED, &peer_credentials, &peer_credentials_size) != 0) {
        perror("getsockopt()");
        exit(EXIT_FAILURE);
    }

    if (peer_credentials.pid != mechanism_pid) {
        printf("The connecting peer's PID did not match the launched mechanism PID\n");
        exit(EXIT_FAILURE);
    }

    // ok now receive the file descriptor from the mechanism




}
