Privilege-Elevation
===================

Executing `privilege-elevation` should demonstrate privilege elevation in terms of opening of a serial port. This is to achieve least-privilege when it comes to accessing rs232 peripherals like Arduino devices.

You pass in serial port path, it will first try to open it using an unprivileged child process, if it fails, it will make use of the Polkit infrastructure and `pkexec` the child process to open it. If the appropriate action file is installed, Polkit will prompt the user to authorise this opening the serial port action. If this succeeds, the file descriptor to the serial port is passed back to the parent process using a unix domain socket. The child process is kept simple and terminates immediately, the superuser privileges only exist temporally for this action and is not kept around.

This is different from normal Polkit programs who have a long running daemon.
As that would mean the daemon is the one that is already authorised with superuser
privileges, and a client program attempts a task by asking the daemon. The daemon
uses Polkit to check whether the client is allowed, and if allowed performs the task. Using a daemon would be more suitable for long running tasks that require repeated authorised actions, but for single one-off actions, this achieves least-privilege better.

Although this is bound to Linux Polkit, the same principle exists on Windows with UAC.

Usage
-----

You need `socat` to create virtual serial ports, `picocom` to act as terminals to the serial ports, `polkit` as the daemon to manage privilege elevation, and a `polkit` agent running in your desktop environment to provide GUI prompt for user authorisation. If you cannot run a GUI `polkit` agent, you can use `pkttyagent --process $$` but then make sure the PID passed into the `--process` option is the PID of the terminal process where you'll run `privilege-elevation`. Also `sudo` is required to make the virtual serial ports owned by `root` which forces elevated open.

Open 3 terminals. On the first one run (this will tell you the path to serial ports):

```sh
sudo socat -d -d pty,raw,echo=0 pty,raw,echo=0
```

On the second terminal, run:

```sh
sudo picocom --baud 9600 --echo <path/to/serial/port1>
``` 

On the third terminal, run:

```sh
privilege-elevation --baud=9600 </path/to/serial/port2>
```

Also use: 

```sh
pkaction --action-id ai.matrix.pkexec.privilege-elevation.open-serial-device
```

This will check whether `pkexec` can find the appropriate action policy file. It will still work even without the action file being installed, but you won't get a nice polkit prompt message.

Installation
-------------

### If on Nix:

Download the release tarball or `git clone`:

```sh
cd ./Privilege-Elevation
nix-build
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

If you don't clean the root, `nix-build` won't compile again, and this may result in incorrect environment variables or macros being used. Repeated invocations of `nix-build` will replace the old `./result` symlink, however the store path will still exist and will need to be manually deleted with `nix-store --delete` or through garbage collection.

There's an issue with setuid binaries inside `nix-shell`, so we have to exit the `nix-shell` to properly execute `pkexec`. This also means `polkit` is not a build input to he `shell.nix`, even though it is to the `default.nix`.
