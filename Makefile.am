pkexec_path = @PKEXEC_PATH@
mechanisms_path = $(pkglibexecdir)
mechanism_path = $(mechanisms_path)/open-serial-device
polkitactiondir = $(datadir)/polkit-1/actions

AM_CFLAGS = -Wall

bin_PROGRAMS = privilege-elevation

privilege_elevation_SOURCES = src/privilege-elevation.c src/protocol.h argparse/argparse.h
privilege_elevation_CFLAGS = -DPKEXEC_PATH=\"$(pkexec_path)\" -DMECHANISM_PATH=\"$(mechanism_path)\"
privilege_elevation_LDADD = argparse/libargparse.a
privilege_elevation_LDFLAGS = -lm

pkglibexec_PROGRAMS = open-serial-device

open_serial_device_SOURCES = src/open-serial-device.c src/baudrates.h src/protocol.h argparse/argparse.h
open_serial_device_LDADD = argparse/libargparse.a
open_serial_device_LDFLAGS = -lm

noinst_LIBRARIES = argparse/libargparse.a
argparse_libargparse_a_SOURCES = argparse/argparse.c argparse/argparse.h
argparse_libargparse_a_CFLAGS = -fPIC
argparse_libargparse_a_AR = $(AR) rcs

dist_polkitaction_DATA = policy/ai.matrix.pkexec.privilege-elevation.policy

EXTRA_DIST = README.md default.nix shell.nix

install-data-hook:
	sed --in-place --expression='s/MECHANISM_PATH/$(subst /,\/,$(mechanism_path))/g' $(DESTDIR)$(datadir)/polkit-1/actions/ai.matrix.pkexec.privilege-elevation.policy
