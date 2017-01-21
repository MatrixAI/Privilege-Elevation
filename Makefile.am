pkexec_path = @PKEXEC_PATH@
mechanisms_path = $(pkglibexecdir)
mechanism_path = $(mechanisms_path)/open-serial-device
polkitactiondir = $(datadir)/polkit-1/actions

AM_CFLAGS = -Wall

bin_PROGRAMS = privilege-elevation

privilege_elevation_SOURCES = src/privilege-elevation.c
privilege_elevation_CFLAGS = -Iargparse -DPKEXEC_PATH=\"$(pkexec_path)\" -DMECHANISM_PATH=\"$(mechanism_path)\"
privilege_elevation_LDADD = argparse/libargparse.a

pkglibexec_PROGRAMS = open-serial-device

open_serial_device_SOURCES = src/open-serial-device.c
open_serial_device_CFLAGS = -Iargparse
open_serial_device_LDADD = argparse/libargparse.a

dist_polkitaction_DATA = policy/ai.matrix.pkexec.privilege-elevation.policy

install-data-hook:
	sed --in-place --expression='s/MECHANISM_PATH/$(subst /,\/,$(mechanism_path))/g' $(DESTDIR)$(datadir)/polkit-1/actions/ai.matrix.pkexec.privilege-elevation.policy

argparse/libargparse.a: Makefile
	cd argparse && $(MAKE) $(AM_MAKEFLAGS) all

check-local:
	cd argparse && $(MAKE) $(AM_MAKEFLAGS) test

clean-local:
	cd argparse && $(MAKE) $(AM_MAKEFLAGS) clean && rm -f *.so

EXTRA_DIST = README.md default.nix shell.nix argparse
