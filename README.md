# tomu-mpris

MPRIS2 D-Bus bridge for **[tomu](https://github.com/6z7y/tomu)**.

Exposes tomu playback over the MPRIS D-Bus interface.

## How It Works

```
┌─────────────────┐       /tmp/tomu.inf         ┌─────────────────┐
│                 │ ──────────────────────────▶ │                 │
│      tomu       │        (poll 100ms)         │   tomu-mpris    │
│   (server)      │                             │   (mpris bridge)│
│                 │ ◀────────────────────────── │                 │
└─────────────────┘       /tmp/tomu.cmd         └────────┬────────┘
                           (commands)                    │
                                                         │ D-Bus
                                                         ▼
                                                ┌─────────────────┐
                                                │  D-Bus clients  │
                                                │ playerctl       │
                                                │ desktop widgets │
                                                │ media keys      │
                                                └─────────────────┘
```


## Install

### Dependencies

- `dbus-1` (libdbus-1-dev)

```bash
# Debian/Ubuntu
sudo apt install libdbus-1-dev

# Arch
sudo pacman -S dbus

# Fedora
sudo dnf install dbus-devel
```

### Build & Install

```bash
make
sudo make install
```

## Usage

Start tomu server first, then run tomu-mpris:

```bash
tomu /path/to/music
tomu-mpris &
```

Control with playerctl:

```bash
playerctl play-pause
playerctl next
playerctl metadata
```

## Supported MPRIS Methods

| Method | Command |
|---|---|
| PlayPause | `PLAYPAUSE` |
| Next | `NEXT` |
| Previous | `PREV` |
... later
