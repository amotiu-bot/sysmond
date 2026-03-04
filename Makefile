CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?=

TARGET  = sysmond
SRC     = src/sysmond.c

PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/sbin
CONFDIR ?= /etc
SYSTEMD_DIR ?= /lib/systemd/system

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) 

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(CONFDIR)
	install -m 0644 sysmond.conf $(DESTDIR)$(CONFDIR)/sysmond.conf
	install -d $(DESTDIR)$(SYSTEMD_DIR)
	install -m 0644 systemd/sysmond.service $(DESTDIR)$(SYSTEMD_DIR)/sysmond.service
