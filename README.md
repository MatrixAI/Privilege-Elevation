We need a build a folder with libexec folder storing the mechanism executable. The client just compiles to main.

The `<annotate key="org.freedesktop.policykit.exec.path">/usr/libexec/file-descriptor-example/file-descriptor-mechanism</annotate>` needs to be changed to the nix/store path upon installation.

Since make install make install into /usr/libexec in normal distributions, it should be possible to make Nix rewrite the path.

On the other hand, perhaps the package itself will supply an action file that will just be interpolated with the correct path. The package will obviously be relevant to the distribution.

Also makefile is intended to create install into $out:

```
mkdir --parents "$out/libexec/file-descriptor-example"

```

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

https://github.com/edrosten/autoconf_tutorial

> Autoconf essentially runs the preprocessor on your script to produce a portable shell script which will perform all the requisite tests, produce handy log files, preprocess template files, for example to generate Makefile from Makefile.in and and take a standard set of command line arguments.

So I guess that means making sure that the action file is in fact generated automatically via a macro?

So this is how its done, I just read the NetworkManager source file, and there's a policy folder, and itself contains the policy as a macro file!

The `*.am` are automake files. So they generate makefile.in, which then does other things.

Ok we just have read more of automake and how it uses the macros. Because specifically we would need to apply the libexec path to both the policy file itself, and the source file (on the path to fork and exeec), prior to building.

---

```
CC=@CC@
LD=@CC@
CFLAGS=@CFLAGS@
LDFLAGS=@LDFLAGS@
LIBS=@LIBS@

SRCDIR = src
OUTDIR = build

# targets that are not files
.PHONY: default all install clean

# the default target???
.DEFAULT_GOAL := default

# how to get 2 things built for libexec as well?

# remember building the executable may require multiple linking
# whereas building a single object only needs include directory to have the header files
# headers and source is compiled into .o, multiple .o is combined into executable
# that's why $^ takes multiple object files
# $< takes only 1 file, the source file
# of course u can also pass headers.. etfc

# configure.ac is compiled to create configure (ahead of time via autotools, and u commit the configure.ac right?)
# which is a shell script that compiles the Makefile.in based on whatever it figured out
# it creates a Makefile
# from there the Makefile is what builds the project
# which means with regards to changing the and substituting the names, we could either process the policy file, or pass CPP macros into the C files
# or something like that

default: $(OUTDIR)/file-descriptor-example
all: default

$(OUTDIR)/file-descriptor-example: $(OUTDIR)/file-descriptor-example.o
  $(LD) $(LDFLAGS) $(LIBS) $^ -o $@

$(OUTDIR)/file-descriptor-example.o: $(SRCDIR)/file-descriptor-example.c
  $(CC) $(CFLAGS) -c $< -o $@


# remove the executable and the object file
# we do not specify how to compile, that's inherited
# and CFLAGS was specified automatically


clean:
  rm -f program *.o





# CC ?= gcc
# CFLAGS += -g -Wall -Wextra

# .PHONY: default all clean

# default: file-descriptor-example

# all: default

# %.o: %.c
#   gcc -c $< -o $@

# file-descriptor-example: file-descriptor-example.o
#   gcc file-descriptor-example-o -o file-descriptor-example


# program.o: program.c
#   gcc -c program.c -o program.o

# program: program.o
#   # compile program.o
#   gcc program.o -o program


# file-descriptor-example: file-descriptor-example.o
#   $(LD) $^ -o $@

# # generic compiler to object file
# # each .c file should be able to compiled into an .o file and only need linking later
# %.o: %.c
#   $(CC) $(CFLAGS) -c $< -o $@

# # use $(LD) or $(CC)
# # no idea how this is meant to work...

# clean:
#   rm -rf build/*
```

Or `autoreconf` is enough.

```
autoreconf --force --install --verbose
./configure
# try this
make
make install
# or this
make distcheck
make dist
```

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