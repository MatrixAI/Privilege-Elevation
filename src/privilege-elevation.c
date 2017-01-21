#include <stdlib.h>     // EXIT_FAILURE, exit(), mkdtemp(), getenv(), atexit()
#include <stdio.h>      // printf(), snprintf(), remove()
#include <stddef.h>     // NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // size_t, strcmp(), strcpy()
#include <unistd.h>     // read(), write(), fork(), exec()
#include <errno.h>      // perror()
#include <sysexits.h>
#include <ftw.h>        // nftw()
#include <libgen.h>     // basename()
#include <assert.h>     // assert()
#include <sys/socket.h> // PF_UNIX, SOCK_STREAM, socklen_t, struct ucred, socket(), bind(), listen(), accept(), getsockopt
#include <sys/un.h>     // UNIX_PATH_MAX, struct sockaddr_un
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid()
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

void cleanup_and_exit() {

    if (unix_sock_dir && *unix_sock_dir) {

        ntfw(unix_sock_dir, ntfw_callback, 64, FTW_DEPTH | FTW_PHYS);

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
    const char * * argv_ = malloc(sizeof (char *) * argc);
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

    // attempt unprivileged open-serial-device
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

        // try to first execute it in an unprivileged way
        // open-serial-device <serial-port>
        execl(mechanisms_path, mechanisms_name, serial_port, (char *) NULL);

        // if we get here, execl failed, we can write the errno for execl failure
        // errno is an int and not a string, but it is possible to write each byte
        // of the errno as a char, so if errno is 4 bytes, then we are just writing
        // 4 characters
        perror("execl()");
        write(exec_errno_pipefd[1], &errno, sizeof(errno));
        exit(EX_UNAVAILABLE);

    }

    // parent is only reading
    close(exec_errno_pipefd[1]);

    // what would happen if we wrote a null pointer?
    int exec_errno;
    if (read(exec_errno_pipefd[0], &exec_errno, sizeof(exec_errno)) > 0) {

        // the mechanism was never executed
        fprintf(stderr, "Could not execute the mechanism.\n");
        close(exec_errno_pipefd[0]);
        exit(EX_UNAVAILABLE);

    }

    // execl succeeded, the pipe is no longer needed
    close(exec_errno_pipefd[0]);




    // THERE IS A PROBLEM HERE
    // it is possible that the child process exits before opening the unix domain socket
    // if it does so, we will never be able to accept a unix_peer_fd
    // on the other hand, we wait on the PID, it's possible that the process succeeded in opening the unix domain socket and is waiting for us to accept
    // this is a race condition that can only be resolved through using concurrency
    // event driven concurrency to work on 2 different conditions
    // the first condition is process status
    // the second condition should be an accept call
    // there can be a non-blocking accept
    // whiat about a non-blocking wait?
    // select on 2 things?

    // with WNOHANG, as long as waitpid returns 0, this means the child process has not yet returned
    // if it has not yet returned, just sleep for 1 second? or sleep 1 ms
    // it shouldn't take long to open a serial port!

    // this is to set the socket that accepts

    // the best solution appears to be pselect
    // it appears to be an alternative to the self-pipe trick
    // however pselect is not as portable as the self-pipe trick
    // this article explains the main problem: https://lwn.net/Articles/176911/

    // mechanism's unix socket address
    struct sockaddr_un unix_peer_addr = {0};
    socklen_t unix_peer_addr_size = sizeof(unix_peer_addr);

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
