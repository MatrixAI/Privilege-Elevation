Privilege-Elevation
===================

Development:

Run: `nix-shell`.

Run `./bootstrap`.

Run `./configure`, `make`, `make install`.

Check `make distcheck` and `make dist`.

Executing privilege-elevation should demonstrate the opening of a serial port.
So you pass a serial port in. Upon attempting to execute it, it is going to run
pkexec of open-serial-device.c. This will cause a pkexec prompt to showup and
follow the polkit policy that has been installed. Upon succeeding authorisation,
the command is then to pass a file descriptor over a UNIX socket back to the
original program, which is privilege-elevation.c.

This is different from normal polkit programs who have a long running daemon.
As that would mean the daemon is the one that is already authorised with superuser
privileges, and a client program attempts a task by asking the daemon. The daemon
uses polkit to check whether the client is allowed, and if allowed performs the task.

We don't have a daemon here, so we rely on pkexec to launch an authorised by superuser
program and use the same functionality to trigger a privilege elevation request.

https://lwn.net/Articles/176911/
https://jineshkj.wordpress.com/2008/02/02/why-pselect/
https://linux.die.net/man/2/pselect
http://pubs.opengroup.org/onlinepubs/009695399/functions/accept.html
https://linux.die.net/man/2/waitpid
https://cr.yp.to/docs/selfpipe.html

> File descriptors can be sent from one process to another by two means. One way is by inheritance, the other is by passing through a unix domain socket. There are three reasons I know of why one might do this. The first is that on platforms that don't have a credentials passing mechanism but do have a file descriptor passing mechanism, an authentication scheme based on file system privilege demonstration could be used instead. The second is if one process has file system privileges that the other does not. The third is scenarios where a server will hand a connection's file descriptor to another already started helper process of some kind. Again this area is different from OS to OS. On Linux this is done with a socket feature known as ancillary data.

> It works by one side sending some data to the other (at least 1 byte) with attached ancillary data. Normally this facility is used for odd features of various underlying network protocols, such as TCP/IP's out of band data. This is accomplished with the lower level socket function sendmsg() that accepts both arrays of IO vectors and control data message objects as members of its struct msghdr parameter. Ancillary, also known as control, data in sockets takes the form of a struct cmsghdr. The members of this structure can mean different things based on what type of socket it is used with. Making it even more squirrelly is that most of these structures need to be modified with macros. Here are two example functions based on the ones available in Warren Gay's book mention at the end of this article. A socket's peer that read data sent to it by send_fd() without using recv_fd() would just get a single capital F.

* http://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c

---

NIXOS/NIX

Remember everything is built inside the `/tmp/...` directory which is notated as "$out". The make file should itself understand to put thigns there into "$out". But where is "$out"? It's not the `/tmp/...` path right, it must be the nix store path!

Another option is:

```
  patchPhase = ''
    substituteInPlace Makefile.am --replace "/usr" ""
  '';
```

Which I guess runs a subtitution on the Makefile itself, replacing `/usr` with the real path.

A default.nix inside this repo would be an outside of nixpkgs tree. This is because this an example application, and not intended to be part of the official nixpkgs distribution or release. To run this default.nix, it's easy. It's possible to import the file directly into configuration.nix and run it as asystem package. Or better yet, run `nix-build` directly on it, and you get a corresponding release file which is a symlink to the built folder. It's described here: https://nixos.org/nixos/manual/index.html#sec-custom-packages

```
git clone this-repo
nix-build this-repo # or nix-build this-repo/default.nix
./result/bin/file-descriptor-client
```

```
environment.systemPackages = [ (import ./my-hello.nix) ];
```

Using it this way, is actually quite similar to writing a npm config file or a composer config file.. etc. Basically it's not submitted to the public index which in this case is the nixpkgs github repo and associated channel release mechanics. And using `nix-build` is the same as using `composer install https://github.com/...`. Except you need to git-clone it first.

Note that nix-copy-closure is not relevant, and neither is nix-install-package (this is being deprecated too).

While `nix-build` would build thepackage, what actually installs it into the user environment? It would require using `nix-env` right? Yea it's actually `nix-env` that installs it into the environment. So we should be using that:

```
# get specific tree
nix-env --file https://github.com/....tar.gz --install file-descriptor-example-0.0.1

# get generic tree
nix-env --file https://github.com/....tar.gz --install file-descriptor-example
```

This is especially true, since we need to activate the capability of polkit, and nix-build wouldn't be able to do this!

To uninstall it appears to be like:

```
nix-env --uninstall file-descriptor-example
```

Remember names must be unique when put together, so this ends up working...

I also think different versions can be installed, but must have different names.

The build  folder will be:

```
├───bin
├───libexec
│   └───file-descriptor-example
└───share
    ├───man
    │   └───man1
    └───polkit-1              
```

This is what needs to built in "$out".

Now I need to know how the crap the paths are being redefined for libexec.

Turns out that this is done with autoconf. Here is an example:

https://www.gnu.org/software/gnats/doc/gnats-4.1.999/html_node/exec_002dprefix.html

Gstreamer has a similar situation, with one of their executables being part of:

```
  outputs = [ "out" "dev" ];
  outputBin = "dev";

  preConfigure = ''
    configureFlagsArray+=("--exec-prefix=$dev")
  '';
```

Note how it sets up the `--exec-prefix` to be equal to `$dev`.

[ "bin" "dev" "out" "doc" ]

What does each mean?

bin is the executables, doc is the is the manual

but dev and out!?

${pkgs.file-descriptor-example}/bin/file-descriptor-client should always work.

out is for any files that were not captured in bin/dev/doc.

I think:

bin -> executables, libexecs, shared objects necessary for execution
dev -> shared objects necessary for further development or binding and headers for inclusion
doc -> manual, html manual.. etc
out -> everything else

These include C(++) headers, pkg-config, cmake and aclocal files.

There's a few others: https://github.com/NixOS/nixpkgs/blob/master/doc/multiple-output.xml

Need to do this on a Linux computer.

On the maintainer:

```
autoreconf --force --install --verbose
./configure
make distcheck
make dist
```

On the end user:

```
tax xvfz ./package.tar.gz
cd package
./configure
make
make install
```
