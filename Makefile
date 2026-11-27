# CookieOS Server 2026
# SPDX-License-Identifier: GPL-2.0-only

.PHONY: all boot clean

all: boot

boot:
	$(MAKE) -C boot

clean:
	$(MAKE) -C boot clean
