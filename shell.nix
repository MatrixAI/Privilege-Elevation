# unlike default.nix, this needs to setup the nix-shell environment 
# for developing this project
# this means propagatedBuildInputs, as the build is not done automatically
# oh we may need to explicitly pass in autoconf, automake.. etc to do autoreconfHook explicitly
# also we can specify third party src, using srcs attribute
# bring in external dependencies
# but that means they are not part of this repo explicitly
# let us use srcs to specify the external dependency here...
# wait... shouldn't it be like a propagatedBuildInputs
# I don't really understand how multiple sources work with regards to $out.. etc
# and how that become part of the buildInputs!?
# we can use this function to take another nix expression that defines
# the building of argparse
# and then have it available in the current environment
# but that's only if we care about the other package being a separate package
# preferably we just define it here
# but what if we could just bring in default.nix defining the current package?
# https://ocharles.org.uk/blog/posts/2014-02-04-how-i-develop-with-nixos.html
{ pkgs ? import <nixpkgs> {} }:

    # imports pkgs into current namespace
    with pkgs;

    stdenv.mkDerivation {

        name = "privilege-elevation-0.0.1";

        # src for local package
        src = ./.;

        # autoreconfHook will run autoreconf --install --force --verbose
        # polkit will be added to the PATH environment
        propagatedBuildInputs = [ autoreconfHook polkit ];

    }