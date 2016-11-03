#include <stdlib.h>     // EXIT_FAILURE, exit(), mkdtemp(), getenv(), atexit()
#include <stdio.h>      // snprintf(), remove()
#include <stddef.h>     // NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // size_t, strcmp(), strcpy()
#include <unistd.h>     // read(), write()
#include <errno.h>      // perror()
#include <ftw.h>        // nftw()
#include <assert.h>     // assert() 
#include <sys/socket.h> // PF_UNIX, SOCK_STREAM, socket(), socklen_t
#include <sys/un.h>     // UNIX_PATH_MAX, struct sockaddr_un
#include <sys/types.h>  // pid_t

#if !defined(PKEXEC_PATH) || !defined(MECHANISM_PATH)
    #error "PKEXEC_PATH and MECHANISM_PATH must be defined."
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

int main() {

    atexit(cleanup_and_exit);

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

    // Use the MECHANISM_PATH to run the executable file-descriptor-mechanism
    // Don't we need the path to PKEXEC as well?
    // Becuase you might have access to the PATH
    // then you need to have the ability to execute PKEXEC directly..?

    // the correct method is to allow the autoconf to set the path to PKEXEC
    // this is because while the mechanism is part of the automake since its part of the package itself, so we always know that it will be part of the libexec directory structure
    // pkexec is not part of the actual package
    // so that means autoconf should find where pkexec is
    // and pass it in as a CFLAG macro
    // and we can use the CFLAG macro as the path to pkexec
    // once we have the full paths to pkexec and the mechanism
    // we can perform an exec call, and get the ball rolling
    // this also allows Nix which does know the exact path of polkit since it declares package dependencies to automatically pass the path of polkit's pkexec and override the configure call
    // autoconf has some facilities for this, but i need to know if the paths can be overridden by the configure call
    // normally the autoconf if there's no passing, it will just search the PATH environment variable for the location of pkexec...
    // note that pkexec becomes both a compile-time and run-time dependency in this case
    // and nix is apparently smart enough to figure out this both!
    // now without the macro being defined, then its possible to just look for pkexec in PATH
    // but is that going to be a problem?
    // some have said looking for sudo via PATH is difficult...
    // we just need to try using execlp variants...

    // I just simplified to just ensure that PKEXEC_PATH and MECHANISM_PATH
    // must be defined prior to compilation


    // SEND DATA TO THE SOCKET!!!
    // 
    // 


    // at some point this needs to call an executable to libexec
    // that means we need to apply a configure autoconf rule
    // that changes the path that this executable things its other binaries and libraries are
    // how do we do this?
    // well configure has a prefix option
    // how do we make use of the prefix option to change the path?
    // binary substitution, or something else?
    // note this isn't like shared libraries or dynamic loader paths, because this path would be specified as part of fork and exec or execve...




    int unix_conn_fd;
    pid_t mechanism_proc;

}