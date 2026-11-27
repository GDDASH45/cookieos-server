# CookieOS Server 2026
# SPDX-License-Identifier: GPL-2.0-only

.PHONY: all boot kernel clean

all: boot kernel

boot:
	$(MAKE) -C boot

kernel:
	$(MAKE) -C kernel

clean:
	$(MAKE) -C boot clean
	$(MAKE) -C kernel clean