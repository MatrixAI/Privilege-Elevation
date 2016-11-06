polkitactiondir = $(datadir)/polkit-1/actions
dist_polkitaction_DATA = ai.matrix.pkexec.privilege-elevation.policy

install-data-hook:
    sed --in-place --expression='s/MECHANISM_PATH/$(pkexec_path)/g' $(DESTDIR)$(datadir)/polkit-1/actions/ai.matrix.pkexec.privilege-elevation.policy