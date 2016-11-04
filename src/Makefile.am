bin_PROGRAMS = file_descriptor_client
file_descriptor_client_SOURCES = file-descriptor-client.c
file_descriptor_client_CFLAGS = -DPKEXEC_PATH=\"$(pkexec_path)\" -DMECHANISM_PATH=\"$(mechanism_path)\"

pkglibexec_PROGRAMS = file_descriptor_mechanism
file_descriptor_mechanism_SOURCES = file-descriptor-mechanism.c

# need to override the installation name of mechanism with mechanism_name
# from mechanism_path (basename $(mechanism_path))