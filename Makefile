# CookieOS Server 2026
# SPDX-License-Identifier: GPL-2.0-only

.PHONY: all boot kernel drivers clean

all: boot kernel drivers

boot:
	$(MAKE) -C boot

kernel:
	$(MAKE) -C kernel

drivers:
	$(MAKE) -C drivers

clean:
	$(MAKE) -C boot clean
	$(MAKE) -C kernel clean