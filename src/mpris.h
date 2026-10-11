#ifndef MPRIS_H
#define MPRIS_H

#include <stdio.h>
#include <stdint.h>
#include <dbus/dbus.h>

#define BUS_NAME    "org.mpris.MediaPlayer2.tomu"
#define OBJ_PATH    "/org/mpris/MediaPlayer2"
#define IFACE_ROOT  "org.mpris.MediaPlayer2"
#define IFACE_PLAYER "org.mpris.MediaPlayer2.Player"
#define IFACE_PROPS "org.freedesktop.DBus.Properties"

#define STATE_FILE  "/tmp/tomu.fifo"
#define CMD_FILE    "/tmp/tomu.cmd"

typedef struct {
  char status[16];
  char artist[128];
  char title[256];
  char album[128];
  char album_artist[128];
  char composer[128];
  char genre[64];
  char date[32];
  char track[32];
  char disc[32];
  char cover_path[512];
  char url[256];

} Metadata;

typedef struct {
  char loop[16];
  int duration;
  int position;
  double volume;
  double speed;
  int shuffle;
} Information;

typedef struct {
  FILE *fifo_file;
  Information inf;
  Metadata data;
} MprisState;

int  state_load(MprisState *st);
void cmd_write(const char *cmd);
void cmd_write_fmt(const char *fmt, ...);

void mpris_init_dbus(DBusConnection **conn);
void mpris_dispatch(DBusConnection *conn, MprisState *st);
void mpris_notify_change(DBusConnection *conn, MprisState *st);

#endif
