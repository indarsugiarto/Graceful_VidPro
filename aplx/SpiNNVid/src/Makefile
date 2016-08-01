MKFILES = Makefile.3 Makefile.5

RM := \rm -rf
LS := \ls -l
MK := \make -f

all: $(APPS)
	@$(MK) Makefile.3
	@$(MK) Makefile.5
	@$(LS) ../../binaries/*.aplx

clean: $(APPS)
	@$(MK) Makefile.3 clean
	@$(MK) Makefile.5 clean
	@$(RM) ../../binaries/*.aplx
