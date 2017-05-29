Privilege-Elevation
===================

Installation
-------------

### If on Nix:

Download the release tarball or `git clone`:

```sh
nix-build ./Privilege-Elevation
./result/bin/Privilege-Elevation/privilege-elevation
```

The above will build into the NixOS store and leave a symlink to access the built folder.

To actually install it into your profile (so you can call it from PATH):

```sh
nix-env --file ./Privilege-Elevation --install privilege-elevation
```

To uninstall it:

```sh
nix-env --uninstall privilege-elevation
```

### Not on Nix:

Download the release tarball:

```sh
tax xvfz ./privilege-elevation-X.X.X.tar.gz
cd privilege-elevation-X.X.X
./configure
make
make install
```

Git clone:

```sh
git clone https://github.com/MatrixAI/Privilege-Elevation.git 
cd Privilege-Elevation
./bootstrap
./configure
make
make install
```

To uninstall it:

```sh
make uninstall
```

Development
------------

On Nix supported system, first setup the Nix shell by running `nix-shell` inside the root of this repository. Still trying to make `nix-shell` run `./bootstrap` and `./configure` prior to launching.

Run these at the root of the project:

```sh
./bootstrap
./configure
make distcheck
make dist
``` 

To check if Nix building works:

```sh
make clean
nix-build
```

If you don't clean the root, `nix-build` won't compile again, and this may result in incorrect environment variables or macros being used.

Usage
-----

You need `socat` to create virtual serial ports, `picocom` to act as terminals to the serial ports, `polkit` as the daemon to manage privilege elevation, and a `polkit` agent running in your desktop environment to provide GUI prompt for user authorisation. If you cannot run a GUI `polkit` agent, you can use `pkttyagent --process $$` but then make sure the PID passed into the `--process` option is the PID of the terminal process where you'll run `privilege-elevation`.

Open 3 terminals. On the first one run (this will tell you the path to serial ports):

```sh
socat -d -d pty,raw,echo=0 pty,raw,echo=0
```

On the second terminal, run:

```sh
picocom --baud 9600 --imap crclf --echo <path/to/serial/port1>
``` 

On the third terminal, run:

```sh
privilege-elevation --baud=9600 </path/to/serial/port2>
```

---

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

---

`$out` is the nix store path.

The `default.nix` can be used to create out-of-tree Nixpkgs packages. The resulting nix file can imported into a configuration.nix or nix-build, or used by nix-env.

The built folder should look something like:

```
result
├── bin
│   └── privilege-elevation
├── libexec
│   └── privilege-elevation
│       └── open-serial-device
└── share
    └── polkit-1
        └── actions
            └── ai.matrix.pkexec.privilege-elevation.policy
```

This is what needs to built in "$out".

How to push things into libexec?

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

Apparently the $outputLib is meant for libraries, residing in lib or libexec.

The most important is to figure out how to install the policy file. But that might be automatic.

Remember that network-manager has one too.

Wait it's just symlinks. I think.

