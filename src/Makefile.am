AM_CFLAGS = -Wall

bin_PROGRAMS = privilege-elevation
privilege_elevation_SOURCES = $(srcdir)/privilege-elevation.c
privilege_elevation_CFLAGS = -I$(top_srcdir)/argparse -DPKEXEC_PATH=\"$(pkexec_path)\" -DMECHANISMS_PATH=\"$(mechanisms_path)\"
privilege_elevation_LDADD = $(top_builddir)/argparse/libargparse.a

pkglibexec_PROGRAMS = open-serial-device
open_serial_device_SOURCES = $(srcdir)/open-serial-device.c
open_serial_device_CFLAGS = -I$(top_srcdir)/argparse
open_serial_device_LDADD = $(top_builddir)/argparse/libargparse.a