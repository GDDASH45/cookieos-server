# CookieOS Server 2026 - driver build system
# SPDX-License-Identifier: GPL-2.0-only

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
DRIVER_DIRS := $(sort $(patsubst %/,%,$(wildcard */)))

.PHONY: all clean

all:
	@set -e; \
	for dir in $(DRIVER_DIRS); do \
		if find "$$dir" -type f -name '*.c' -print -quit | grep -q .; then \
			echo "  BUILD   $$dir"; \
			$(MAKE) --no-print-directory -C "$$dir" -f "$(MAKEFILE_DIR)driver.mk"; \
		fi; \
	done

clean:
	@set -e; \
	for dir in $(DRIVER_DIRS); do \
		if find "$$dir" -type f -name '*.c' -print -quit | grep -q .; then \
			$(MAKE) --no-print-directory -C "$$dir" -f "$(MAKEFILE_DIR)driver.mk" clean; \
		fi; \
	done
