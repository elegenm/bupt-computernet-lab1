CC ?= gcc
CFLAGS ?= -O2 -g
CPPFLAGS ?= -I.
LDFLAGS ?=
LDLIBS ?= -lm

GBN_BIN := gbn_exe
SR_BIN := sr_exe
STOPWAIT_BIN := datalink

GBN_SRCS := datalink_gobackn.c protocol.c lprintf.c crc32.c
SR_SRCS := datalink_selective.c protocol.c lprintf.c crc32.c
STOPWAIT_SRCS := datalink.c protocol.c lprintf.c crc32.c

GBN_UTOPIA_PORT := 59202
GBN_DEFAULT_PORT := 59204
GBN_HIGH_BER_PORT := 59203
SR_UTOPIA_PORT := 59231
SR_DEFAULT_PORT := 59232
SR_HIGH_BER_PORT := 59233

GBN_UTOPIA_TIME := 10
GBN_DEFAULT_TIME := 15
GBN_HIGH_BER_TIME := 12
SR_UTOPIA_TIME := 10
SR_DEFAULT_TIME := 15
SR_HIGH_BER_TIME := 12

HIGH_BER := 1e-4
START_DELAY := 1

.PHONY: all help clean \
	build build-gbn build-sr build-stopwait \
	gbn-utopia gbn-default gbn-high-ber \
	sr-utopia sr-default sr-high-ber \
	run-all

all: build

help:
	@printf '%s\n' \
	'build targets:' \
	'  make build         # build GBN and SR' \
	'  make build-gbn     # build gbn_exe' \
	'  make build-sr      # build sr_exe' \
	'  make build-stopwait# build original datalink' \
	'' \
	'run targets:' \
	'  make gbn-utopia    # GBN, -f -u' \
	'  make gbn-default   # GBN, -f with default BER' \
	'  make gbn-high-ber  # GBN, -f -b $(HIGH_BER)' \
	'  make sr-utopia     # SR, -f -u' \
	'  make sr-default    # SR, -f with default BER' \
	'  make sr-high-ber   # SR, -f -b $(HIGH_BER)' \
	'  make run-all       # run all six demo cases in sequence'

build: build-gbn build-sr

build-gbn: $(GBN_BIN)

build-sr: $(SR_BIN)

build-stopwait: $(STOPWAIT_BIN)

$(GBN_BIN): $(GBN_SRCS) datalink.h protocol.h lprintf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GBN_SRCS) $(LDFLAGS) -o $@ $(LDLIBS)

$(SR_BIN): $(SR_SRCS) datalink.h protocol.h lprintf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SR_SRCS) $(LDFLAGS) -o $@ $(LDLIBS)

$(STOPWAIT_BIN): $(STOPWAIT_SRCS) datalink.h protocol.h lprintf.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(STOPWAIT_SRCS) $(LDFLAGS) -o $@ $(LDLIBS)

define RUN_PAIR
	@echo "Running $(1) on port $(2)"
	@./$(1) $(3) -p $(2) A & \
	pid_a=$$!; \
	sleep $(START_DELAY); \
	./$(1) $(3) -p $(2) B; \
	wait $$pid_a
endef

gbn-utopia: $(GBN_BIN)
	$(call RUN_PAIR,$(GBN_BIN),$(GBN_UTOPIA_PORT),-f -u -t $(GBN_UTOPIA_TIME))

gbn-default: $(GBN_BIN)
	$(call RUN_PAIR,$(GBN_BIN),$(GBN_DEFAULT_PORT),-f -t $(GBN_DEFAULT_TIME))

gbn-high-ber: $(GBN_BIN)
	$(call RUN_PAIR,$(GBN_BIN),$(GBN_HIGH_BER_PORT),-f -b $(HIGH_BER) -t $(GBN_HIGH_BER_TIME))

sr-utopia: $(SR_BIN)
	$(call RUN_PAIR,$(SR_BIN),$(SR_UTOPIA_PORT),-f -u -t $(SR_UTOPIA_TIME))

sr-default: $(SR_BIN)
	$(call RUN_PAIR,$(SR_BIN),$(SR_DEFAULT_PORT),-f -t $(SR_DEFAULT_TIME))

sr-high-ber: $(SR_BIN)
	$(call RUN_PAIR,$(SR_BIN),$(SR_HIGH_BER_PORT),-f -b $(HIGH_BER) -t $(SR_HIGH_BER_TIME))

run-all: gbn-utopia gbn-default gbn-high-ber sr-utopia sr-default sr-high-ber

clean:
	$(RM) $(GBN_BIN) $(SR_BIN) $(STOPWAIT_BIN) *.o *.log
	$(RM) -r *.dSYM
