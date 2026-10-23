PKG_CONFIG ?= pkg-config
PACKAGES := gtk4 libnautilus-extension-4 json-glib-1.0
EXTENSION_DIR ?= $(shell $(PKG_CONFIG) --variable=extensiondir libnautilus-extension-4)
USER_UNIT_DIR ?= $(or $(shell $(PKG_CONFIG) --variable=systemduserunitdir systemd 2>/dev/null),/usr/lib/systemd/user)
UNIT := nautilus-cloud@.service

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
# Stricter checks for the module's own sources.
WARNINGS := -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wformat=2
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) -DG_LOG_DOMAIN=\"nautilus-cloud-sidebar\" \
              -DCLOUD_SYSTEMD_USER_UNIT_DIR=\"$(USER_UNIT_DIR)\"
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES))
# src/core must stay free of GTK: its tests link only these.
CORE_LIBS := $(shell $(PKG_CONFIG) --libs gio-2.0 json-glib-1.0)
# The fake-window test styles itself with libadwaita, like Nautilus.
TEST_CFLAGS := $(shell $(PKG_CONFIG) --cflags libadwaita-1)
TEST_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 gmodule-2.0 json-glib-1.0 libadwaita-1)

TARGET := libnautilus-cloud-sidebar.so
SOURCES := $(wildcard src/*.c src/*/*.c)
OBJECTS := $(SOURCES:src/%.c=build/%.o)
CORE_OBJECTS := $(filter build/core/%,$(OBJECTS))
TESTS := build/test-core build/test-sidebar

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -shared $(LDFLAGS) -Wl,--no-undefined -o $@ $^ $(PKG_LIBS)

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -fPIC -fvisibility=hidden -Isrc $(PKG_CFLAGS) -MMD -MP -c -o $@ $<

build/test-core: tests/test-core.c $(CORE_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $(PKG_CFLAGS) -DCLOUD_SOURCE_DIR=\"$(CURDIR)\" -MMD -MP $(LDFLAGS) \
	    -o $@ $(filter %.c %.o,$^) $(CORE_LIBS)

build/test-sidebar: tests/test-sidebar.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PKG_CFLAGS) $(TEST_CFLAGS) -MMD -MP $(LDFLAGS) -o $@ $< $(TEST_LIBS)

check: $(TARGET) $(TESTS)
	build/test-core
	tests/run-sidebar-test.sh build/test-sidebar $(abspath $(TARGET))

# Runs the installed Nautilus with only this module, in private namespaces.
smoke: $(TARGET)
	tests/run-nautilus-smoke.sh $(TARGET)

# The module, and the systemd user unit that mounts each remote at login.
install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(EXTENSION_DIR)/$(TARGET)
	install -Dm644 data/$(UNIT) $(DESTDIR)$(USER_UNIT_DIR)/$(UNIT)

uninstall:
	rm -f $(DESTDIR)$(EXTENSION_DIR)/$(TARGET) $(DESTDIR)$(USER_UNIT_DIR)/$(UNIT)

# Editor support for clangd.
compile_flags.txt:
	printf '%s\n' -std=gnu11 -Isrc $(PKG_CFLAGS) | tr ' ' '\n' | grep -v '^$$' > $@

clean:
	rm -rf build $(TARGET)

.PHONY: all check smoke install uninstall clean

-include $(OBJECTS:.o=.d) $(TESTS:=.d)
