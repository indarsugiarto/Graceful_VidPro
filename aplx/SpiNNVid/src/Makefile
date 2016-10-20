# What if we compile both spin3 and spin5 version here?
MKFILES = Makefile.3 Makefile.5 Makefile.profiler Makefile.streamer

RM := \rm -rf
LS := \ls -l
MK := \make -f

all: $(APPS)
	@$(MK) Makefile.3
	@$(MK) Makefile.5
	@$(MK) Makefile.profiler
	@$(MK) Makefile.streamer
	@$(LS) ../../binaries/*.aplx

clean: $(APPS)
	@$(MK) Makefile.3 clean
	@$(MK) Makefile.5 clean
	@$(MK) Makefile.profiler clean
	@$(MK) Makefile.streamer clean
	@$(RM) ../../binaries/*.aplx
