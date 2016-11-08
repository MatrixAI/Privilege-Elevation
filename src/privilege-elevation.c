#include <stdlib.h>     // EXIT_FAILURE, exit(), mkdtemp(), getenv(), atexit()
#include <stdio.h>      // snprintf(), remove()
#include <stddef.h>     // NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // size_t, strcmp(), strcpy()
#include <unistd.h>     // read(), write(), fork(), exec()
#include <errno.h>      // perror()
#include <ftw.h>        // nftw()
#include <libgen.h>     // basename()
#include <assert.h>     // assert() 
#include <sys/socket.h> // PF_UNIX, SOCK_STREAM, socklen_t, struct ucred, socket(), bind(), listen(), accept(), getsockopt
#include <sys/un.h>     // UNIX_PATH_MAX, struct sockaddr_un
#include <sys/types.h>  // pid_t
#include "argparse.h"

#define XSTR(s) STR(s)
#define STR(s) #s

#if !defined(PKEXEC_PATH) || !defined(MECHANISMS_PATH)
    #error "PKEXEC_PATH and MECHANISMS_PATH must be defined."
#endif

static char unix_sock_dir[UNIX_PATH_MAX] = {0};

int ntfw_callback(const char * path, const struct stat * sb, int type_flag, struct FTW * ftw_buf) {

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

    // USE ARGPARSE, make sure to make a copy of argv since it changes it
    // and acquire the parameters necessary to choose which action to take
    // like opening a serial port
    // or something else...

    const char socket_name[] = "socket.sock";

    const char * temporary_folder = getenv("TMPDIR");
    if (!temporary_folder) {
        temporary_folder = "/tmp";
    }

    char template[UNIX_PATH_MAX - sizeof(socket_name) + 1]; // +1 for \0 byte
    if (snprintf(
            template, 
            sizeof(template), 
            "%s/%s", 
            temporary_folder, 
            "polkit_demo.XXXXXX"
        ) >= sizeof(template)
    ) {
        printf("$TMPDIR path for saving the socket is too long.");
        exit(EXIT_FAILURE);
    }

    char * directory = mkdtemp(template);
    if (directory == NULL) {
        perror("mkdtemp()");.
        exit(EXIT_FAILURE);
    }
    strcpy(unix_sock_dir, directory);

    char unix_sock_path[UNIX_PATH_MAX];
    snprintf(unix_sock_path, sizeof(unix_sock_path), "%s/%s", unix_sock_dir, socket_name);

    struct sockaddr_un unix_sock_addr = {
        .sun_family = AF_UNIX,
        .sun_path = unix_sock_path
    };

    int unix_sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (unix_sock_fd < 0) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    if (bind(unix_sock_fd, (struct sockaddr *) &unix_sock_addr, sizeof(unix_sock_addr)) != 0) {
        perror("bind()");
        exit(EXIT_FAILURE);
    }

    if (listen(unix_sock_fd, 1) != 0) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }



    pid_t mechanism_pid = fork();

    if (mechanism_pid == -1) {

        perror("fork()");
        exit(EXIT_FAILURE);
    
    } else if (mechanism_pid == 0) {

        // cannot pass a literal into basename because it mutates it
        char pkexec_path[] = STR(PKEXEC_PATH);
        char pkexec_name[] = STR(PKEXEC_PATH);
        pkexec_name = basename(pkexec_name);

        // we have a string literal here, and we need combine it with the actual task to do
        // in this case, it's simply open-serial-device
        char mechanisms_path[] = STR(MECHANISMS_PATH);



        execl(pkexec_path, pkexec_name, mechanisms_path)

        execl(PKEXEC_PATH, pkexec_name);


    } else {

        // we have to deal with the possibility that the child process failed
        // so when we are accepting a connection, there also needs to be a check
        // if the child process failed, and if so, cancel waiting, and exit
        // how do we do that?

        // we only expect 1 connection, so there's no forking the connection handler

        struct sockaddr_un unix_peer_addr;
        socklen_t unix_peer_addr_size = sizeof(unix_peer_addr);
        memset(&unix_peer_addr, 0, unix_peer_addr_size);

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

}