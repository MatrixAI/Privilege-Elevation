
# build inputs is only for compilation
# propagatedBuildInputs is for dependencies that must exist at run time
# in a nix-shell, it builds a shell environment
# which means our build tools, we want to have inside the shell environment
# we want automake, autoconf... etc
# autoreconfHook appears to run autoreconf --install --force --verbose
# that should setup the configure details right?


{ pkgs ? import <nixpkgs> {} }:

    # imports pkgs into current namespace
    with pkgs;

    stdenv.mkDerivation {

        name = "privilege-elevation-0.0.1";

        src = ./.;

        # autoreconfHook will run autoreconf --install --force --verbose
        # polkit will be added to the PATH environment

        propagatedBuildInputs = [ autoreconfHook polkit ];

    }