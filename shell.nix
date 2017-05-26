
# build inputs is only for compilation
# propagatedBuildInputs is for dependencies that must exist at run time
# in a nix-shell, it builds a shell environment
# which means our build tools, we want to have inside the shell environment
# we want automake, autoconf... etc
# autoreconfHook appears to run autoreconf --install --force --verbose
# that should setup the configure details right?
# no it doesn't run it
# putting it into the propagatedBuildInputs allows you to use it, but doesn't run it
# I already boot the necessary detaisl into ./bootstrap
# Older autotools applications are built with ./autogen.sh, but the bootstrap just writes the autoreconf command

{ pkgs ? import <nixpkgs> {} }:

    # imports pkgs into current namespace
    with pkgs;

    stdenv.mkDerivation {

        name = "privilege-elevation";
        src = ./.;

        # autoreconfHook will run autoreconf --install --force --verbose
        # polkit will be added to the PATH environment

        buildInputs = [ autoreconfHook polkit ];

    }
