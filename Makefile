CC           := cc
CFLAGS       := -Wall -Wextra -g -O3 -Iinclude $(shell pkg-config --cflags dbus-1)
LIBS         := -lpthread $(shell pkg-config --libs dbus-1)
INSTALL_PATH := /usr/local/bin

MPRIS_NAME    := tomu-mpris
MPRIS_SRC     := $(wildcard *.c)

# build
all: $(MPRIS_NAME)

$(MPRIS_NAME):
	$(CC) $(MPRIS_SRC) -o $@ $(CFLAGS) $(shell pkg-config --cflags --libs dbus-1) -lpthread


# install
install: all
	@echo "\033[0;33mrequesting root permission, for install tomu\033[0m"
	sudo install -m755 $(MPRIS_NAME) $(INSTALL_PATH)
	@echo "\033[0;32mDone\033[0m"

clean:
	rm -rf $(OBJECT_BUILD_DIR) $(MPRIS_NAME)

update:
	@echo "\033[0;32mFetching latest tomu source...\033[0m"
	git pull

uninstall:
	sudo rm -f /usr/local/bin/tomu-mpris
	@echo "\033[0;32mUninstalled successfully\033[0m"

.PHONY: all install clean uninstall
