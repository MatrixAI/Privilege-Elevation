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

---

Need to use this:

https://github.com/cofyc/argparse

How?

---

Can't the default.nix not only be the package derivation, but also be used to setup the minimal nix-shell environment to imperatively compile the package and anything else. We still have to use `stdenv.mkDerivation`, but instead of just `buildInputs`, there's going to be `propagatedBuildInputs`. Is this harmful in general? Or can I just add it in? Maybe.. we should have different `default.nix`, perhaps a `shell.nix` instead doing the propagatedBuildInputs.

---

How should I handle external dependencies in a C application?

Should I rely on the distribution's packager
Or should I just include the file into my git tree
Or should I use submodules
or is there a package manager sort of thing for C?

And also I guess this also depends on whether the dependency is compile-time or run-time. Because if it's compile-time, there's not much problem with just copying the source files directly in the git tree, and considering it a sub-package. Or use git submodules.

Thing is, I'm using Nix, so why don't I rely on this Nix concept to setup the shell for whatever is a remote dependency.

But then this requires Nix to development, and without Nix, you cannot develop it?

Ok so basically this is how it works.

Let's say your application relies on ffmpeg. During development, you do what is necessary (including just merging the subtree or submodule or relying on distro package headers or rely on a shell.nix file) to bring ffmpeg to your development environment, and get the inclusion headers included. This is where autoconf becomes useful, as the macro system can find ffmpeg libraries (header files and shared objects) for you, and where it can't find it, you can override with a ./configure parameter.

So you develop your application, and you finish, and you decide to distribute. When distribute you create a distribution tarball that only includes your source code (you can also distribute binaries, but that's separate distribution channel, which depends on OS and architecture, and also whether its statically or dynamically linked).

Once you have this done, it's the distribution maintainer's (Debian, Centos.. etc) job to take your source code repository, or your tarball channels, and then to write expressions in their packaging DSL, and produce a workable and installable software via their package manager. This may include setting it up so that your software can find the ffmpeg headers (compile time dependency) or propagating shared objects (runtime dependency). Compile time dependency is quite a bit more simple, whereas dealing with runtime dependencies requires that the link paths are correct which may involve rewriting the paths (just like how Nixos supports this), or recompiling from source with specification of a different path to be linked.

This works quite well, when your dependencies are well known projects that already are packaged into their distributions. Like ffmpeg.

But what if your dependency is some unknown C library that only developers cared about. Say for example: https://github.com/cofyc/argparse

At this point, you can again try to bring in the dependency any way you can, but then when it comes to distribution you now have to consider what to do.

You can:

A. Just bring it into your own source code tree, and distribute as a single common source code tree with both your application and the third party library. This can be done with git subtrees or git submodules or just plain copy and paste.
B. Try to package the third party library as a separate distributable code, target specific distros, like Nixpkgs, debian or rpm, and write both the package manager DSL for the third party library and your code, and submit, and hope they work. So now you have 2 packages instead of 1, and now you're also maintaining the package manager DSL for both packages. Not a bad idea, if the library itself will be seen as useful for the distro community. In the case of Nixos, it's pretty inclusive.

For option A, you will have to statically link into the library or just compile into a single object. Static linking is only necessary if you don't have access to the source code (but you do have access to the headers!). 

For option B, you can statically link or dynamically link to the second package, and this is slightly more scalable, as future projects may also rely on this library, and from there on, you don't need it as part of your distributable source code, but simply part of any package derivation DSL. In the case of nixpkgs, this is just a simple parameter specification of the library package as part of `nixpkgs.pkgs`. For other distros, you may need to look pkg-config and how to set that up.

Also read this: https://news.ycombinator.com/item?id=10658412

Nix is really the solution, but really we need get Nix working on cygwin! Well the shell.nix should be quite useful in this regard for development, and an easy way to specify external sources aswell. But then it makes it difficult to develop on anything that is not Nix based. Then again, this particular library requires Nix anyway!

Using option B, you should use pkg-config to find the libraries that you need, specifically the linking flags. Read this: http://david.rothlis.net/large-gnu-make/

For option A, you will hit up the problem of autotools subpackages, it's not easy to integrate non-autotooled subpackages in an autotooled package.

* https://www.gnu.org/software/automake/manual/html_node/Subpackages.html (for autotools subpackages)
* https://www.gnu.org/software/automake/manual/html_node/Third_002dParty-Makefiles.html#Third_002dParty-Makefiles (for non-autotools third party pacakges)

Isn't there an easy way to do this instead of wrapping their makefiles, can't I just directly build them and bypass their infrastructure?

This is the best resource: https://autotools.io/index.html

---

Using git submodules and:

https://www.gnu.org/software/automake/manual/html_node/Third_002dParty-Makefiles.html#Third_002dParty-Makefiles

and also non-recursive make?

Actually since the subpackage is not an automake package we have to instead use git subtree, since we are going to have to change the repo itself, and add in a `GNUmakefile.in`, that basically wraps the original `Makefile`.

```
git subtree add --prefix argparse https://github.com/cofyc/argparse.git master --squash
```