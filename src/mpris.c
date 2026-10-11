#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <ctype.h>
#include <dbus/dbus.h>

#include "errors.h"
#include "mpris.h"

void fifo_file_reader(MprisState *ctx, int ON)
{
  if (ON) {
    int fd = open(STATE_FILE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) die("fd file:");

    ctx->fifo_file = fdopen(fd, "r");
    if (!ctx->fifo_file) die("Fifo file:");
  }
  else {
    if (ctx->fifo_file) {
      fclose(ctx->fifo_file);
    }
  }
}

static void uri_encode(const char *in, char *out, size_t n) {
  static const char *safe = "/-_.~";
  size_t j = 0;
  for (; *in && j + 4 < n; in++) {
    unsigned char c = *in;
    if (isalnum(c) || strchr(safe, c)) out[j++] = c;
    else j += snprintf(out + j, n - j, "%%%02X", c);
  }
  out[j] = '\0';
}
static void parser_fifo_line(MprisState *st, char *line)
{
  char *sep = strchr(line, ':');
  if (!sep) return;
  *sep = '\0';
  const char *key = line;
  const char *val = sep + 1;

  if      (!strcmp(key, "STATUS"))       strncpy(st->data.status, val, sizeof(st->data.status) - 1);
  else if (!strcmp(key, "ARTIST"))       strncpy(st->data.artist, val, sizeof(st->data.artist) - 1);
  else if (!strcmp(key, "TITLE"))        strncpy(st->data.title, val, sizeof(st->data.title) - 1);
  else if (!strcmp(key, "ALBUM"))        strncpy(st->data.album, val, sizeof(st->data.album) - 1);
  else if (!strcmp(key, "ALBUM_ARTIST")) strncpy(st->data.album_artist, val, sizeof(st->data.album_artist) - 1);
  else if (!strcmp(key, "COMPOSER"))     strncpy(st->data.composer, val, sizeof(st->data.composer) - 1);
  else if (!strcmp(key, "GENRE"))        strncpy(st->data.genre, val, sizeof(st->data.genre) - 1);
  else if (!strcmp(key, "DATE"))         strncpy(st->data.date, val, sizeof(st->data.date) - 1);
  else if (!strcmp(key, "TRACK"))        strncpy(st->data.track, val, sizeof(st->data.track) - 1);
  else if (!strcmp(key, "DISC"))         strncpy(st->data.disc, val, sizeof(st->data.disc) - 1);
  else if (!strcmp(key, "COVER"))        strncpy(st->data.cover_path, val, sizeof(st->data.cover_path) - 1);
  else if (!strcmp(key, "URL"))          strncpy(st->data.url, val, sizeof(st->data.url) - 1);

  else if (!strcmp(key, "LOOP"))         strncpy(st->inf.loop, val, sizeof(st->inf.loop) - 1);
  else if (!strcmp(key, "DURATION"))     st->inf.duration = atoi(val);
  else if (!strcmp(key, "POSITION"))     st->inf.position = atoi(val);
  else if (!strcmp(key, "VOLUME"))       st->inf.volume   = atof(val);
  else if (!strcmp(key, "SPEED"))        st->inf.speed    = atof(val);
  else if (!strcmp(key, "SHUFFLE"))      st->inf.shuffle  = atoi(val);
}


void cmd_write_fmt(const char *fmt, ...) {
  FILE *f = fopen(CMD_FILE, "a");
  if (!f) return;
  flock(fileno(f), LOCK_EX);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fprintf(f, "\n");
  fflush(f);
  flock(fileno(f), LOCK_UN);
  fclose(f);
}

void cmd_write(const char *cmd) {
  cmd_write_fmt("%s", cmd);
}


static void dbus_err_check(DBusError *err, const char *msg) {
  if (dbus_error_is_set(err)) {
    fprintf(stderr, "%s: %s\n", msg, err->message);
    dbus_error_free(err);
    exit(1);
  }
}

static void reply_empty(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  dbus_connection_send(conn, reply, NULL);
  dbus_message_unref(reply);
}

// --- var helpers ---

static void var_string(DBusMessageIter *t, const char *v) {
  DBusMessageIter var;
  dbus_message_iter_open_container(t, DBUS_TYPE_VARIANT, "s", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v);
  dbus_message_iter_close_container(t, &var);
}
static void var_bool(DBusMessageIter *t, int v) {
  DBusMessageIter var;
  dbus_bool_t b = v ? TRUE : FALSE;
  dbus_message_iter_open_container(t, DBUS_TYPE_VARIANT, "b", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
  dbus_message_iter_close_container(t, &var);
}
static void var_double(DBusMessageIter *t, double v) {
  DBusMessageIter var;
  dbus_message_iter_open_container(t, DBUS_TYPE_VARIANT, "d", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_DOUBLE, &v);
  dbus_message_iter_close_container(t, &var);
}
static void var_int64(DBusMessageIter *t, int64_t v) {
  DBusMessageIter var;
  dbus_message_iter_open_container(t, DBUS_TYPE_VARIANT, "x", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_INT64, &v);
  dbus_message_iter_close_container(t, &var);
}
static void var_int32(DBusMessageIter *t, int32_t v) {
  DBusMessageIter var;
  dbus_message_iter_open_container(t, DBUS_TYPE_VARIANT, "i", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &v);
  dbus_message_iter_close_container(t, &var);
}
static void var_emptystrarray(DBusMessageIter *t) {
  DBusMessageIter var, arr;
  dbus_message_iter_open_container(t, DBUS_TYPE_VARIANT, "as", &var);
  dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
  dbus_message_iter_close_container(&var, &arr);
  dbus_message_iter_close_container(t, &var);
}

// --- dict helpers ---

static void dict_add_string(DBusMessageIter *arr, const char *key, const char *val) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(arr, &entry);
}
static void dict_add_strarray1(DBusMessageIter *arr, const char *key, const char *val) {
  DBusMessageIter entry, var, astr;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
  dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &astr);
  dbus_message_iter_append_basic(&astr, DBUS_TYPE_STRING, &val);
  dbus_message_iter_close_container(&var, &astr);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(arr, &entry);
}
static void dict_add_int64(DBusMessageIter *arr, const char *key, int64_t val) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "x", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_INT64, &val);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(arr, &entry);
}
static void dict_add_int32(DBusMessageIter *arr, const char *key, int32_t val) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &val);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(arr, &entry);
}
static void dict_add_path(DBusMessageIter *arr, const char *key, const char *val) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &val);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(arr, &entry);
}

// --- parse /tmp/tomu.inf ---

int state_load(MprisState *st)
{
  if (!st->fifo_file) return -1;

  char line[1024];
  while (fgets(line, sizeof(line), st->fifo_file)) {
    line[strcspn(line, "\n")] = '\0';
    parser_fifo_line(st, line);
  }

  // IMPORTANT: with O_NONBLOCK, stdio sets EOF/error on EAGAIN.
  // Without clearerr, next call returns NULL forever.
  if (feof(st->fifo_file) || ferror(st->fifo_file))
    clearerr(st->fifo_file);

  return 0;
}

// --- append_property (reads from local MprisState) ---

static void append_metadata(MprisState *st, DBusMessageIter *var) {
  DBusMessageIter arr;
  dbus_message_iter_open_container(var, DBUS_TYPE_ARRAY, "{sv}", &arr);

  if (st->inf.duration > 0)
    dict_add_int64(&arr, "mpris:length", (int64_t)st->inf.duration * 1000000);
  if (st->data.artist[0])
    dict_add_strarray1(&arr, "xesam:artist", st->data.artist);
  if (st->data.cover_path[0]) {
    char enc[1800], uri[1900];
    uri_encode(st->data.cover_path, enc, sizeof(enc));
    snprintf(uri, sizeof(uri), "file://%s", enc);
    dict_add_string(&arr, "mpris:artUrl", uri);
  }
  if (st->data.composer[0])
    dict_add_strarray1(&arr, "xesam:composer", st->data.composer);
  if (st->data.track[0])
    dict_add_int32(&arr, "xesam:trackNumber", (int32_t)atoi(st->data.track));
  if (st->data.disc[0])
    dict_add_int32(&arr, "xesam:discNumber", atoi(st->data.disc));
  dict_add_path(&arr, "mpris:trackid", "/org/mpris/MediaPlayer2/tomu/track1");
  if (st->data.title[0])
    dict_add_string(&arr, "xesam:title", st->data.title);
  if (st->data.url[0])
    dict_add_string(&arr, "xesam:url", st->data.url);
  if (st->data.date[0])
    dict_add_string(&arr, "xesam:contentCreated", st->data.date);
  if (st->data.album_artist[0])
    dict_add_strarray1(&arr, "xesam:albumArtist", st->data.album_artist);
  if (st->data.album[0])
    dict_add_string(&arr, "xesam:album", st->data.album);
  if (st->data.genre[0])
    dict_add_strarray1(&arr, "xesam:genre", st->data.genre);

  dbus_message_iter_close_container(var, &arr);
}

static void append_property(MprisState *st, DBusMessageIter *target, const char *prop) {
  if (!strcmp(prop, "PlaybackStatus"))
    var_string(target, st->data.status);
  else if (!strcmp(prop, "LoopStatus"))
    var_string(target, st->inf.loop);
  else if (!strcmp(prop, "Rate"))           var_double(target, st->inf.speed);
  else if (!strcmp(prop, "MinimumRate"))    var_double(target, 1.0);
  else if (!strcmp(prop, "MaximumRate"))    var_double(target, 1.0);
  else if (!strcmp(prop, "Shuffle"))        var_bool(target, st->inf.shuffle);
  else if (!strcmp(prop, "Volume"))         var_double(target, st->inf.volume);
  else if (!strcmp(prop, "Position"))       var_int64(target, (int64_t)st->inf.position * 1000000);
  else if (!strcmp(prop, "Metadata")) {
    DBusMessageIter var;
    dbus_message_iter_open_container(target, DBUS_TYPE_VARIANT, "a{sv}", &var);
    append_metadata(st, &var);
    dbus_message_iter_close_container(target, &var);
  }
  else if (!strcmp(prop, "CanGoNext") || !strcmp(prop, "CanGoPrevious") ||
           !strcmp(prop, "CanPlay")   || !strcmp(prop, "CanPause") ||
           !strcmp(prop, "CanSeek")   || !strcmp(prop, "CanControl") ||
           !strcmp(prop, "CanQuit"))
    var_bool(target, 1);
  else if (!strcmp(prop, "CanRaise") || !strcmp(prop, "HasTrackList"))
    var_bool(target, 0);
  else if (!strcmp(prop, "Identity") || !strcmp(prop, "DesktopEntry"))
    var_string(target, "tomu");
  else if (!strcmp(prop, "SupportedUriSchemes") || !strcmp(prop, "SupportedMimeTypes"))
    var_emptystrarray(target);
}

static const char *PLAYER_PROPS[] = {
  "PlaybackStatus", "LoopStatus", "Rate", "Shuffle", "Metadata", "Volume", "Position",
  "MinimumRate", "MaximumRate", "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause",
  "CanSeek", "CanControl", NULL
};
static const char *ROOT_PROPS[] = {
  "Identity", "CanQuit", "CanRaise", "HasTrackList", "DesktopEntry",
  "SupportedUriSchemes", "SupportedMimeTypes", NULL
};

static void reply_get(DBusConnection *conn, DBusMessage *msg, MprisState *st, const char *iface, const char *prop) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);
  append_property(st, &iter, prop);
  dbus_connection_send(conn, reply, NULL);
  dbus_message_unref(reply);
}

static void reply_get_all(DBusConnection *conn, DBusMessage *msg, MprisState *st, const char *iface) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  DBusMessageIter iter, arr;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr);

  const char **list = !strcmp(iface, IFACE_PLAYER) ? PLAYER_PROPS : ROOT_PROPS;
  for (int i = 0; list[i]; i++) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &list[i]);
    append_property(st, &entry, list[i]);
    dbus_message_iter_close_container(&arr, &entry);
  }

  dbus_message_iter_close_container(&iter, &arr);
  dbus_connection_send(conn, reply, NULL);
  dbus_message_unref(reply);
}

// --- handle_set: write command to tomu.cmd ---

static void handle_set(DBusConnection *conn, DBusMessage *msg) {
  DBusMessageIter iter, var;
  const char *iface, *prop;

  dbus_message_iter_init(msg, &iter);
  dbus_message_iter_get_basic(&iter, &iface); dbus_message_iter_next(&iter);
  dbus_message_iter_get_basic(&iter, &prop);  dbus_message_iter_next(&iter);
  dbus_message_iter_recurse(&iter, &var);
  (void)iface;

  if (!strcmp(prop, "Volume")) {
    double val;
    dbus_message_iter_get_basic(&var, &val);
    cmd_write_fmt("SETVOLUME:%.2f", val);
  }
  else if (!strcmp(prop, "LoopStatus")) {
    const char *val;
    dbus_message_iter_get_basic(&var, &val);
    cmd_write_fmt("SETLOOP:%s", val);
  }
  else if (!strcmp(prop, "Shuffle")) {
    dbus_bool_t val;
    dbus_message_iter_get_basic(&var, &val);
    cmd_write_fmt("SETSHUFFLE:%d", (int)val);
  }
  else if (!strcmp(prop, "Rate")) {
    double val;
    dbus_message_iter_get_basic(&var, &val);
    cmd_write_fmt("SETSPEED:%.2f", val);
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);
  dbus_connection_send(conn, reply, NULL);
  dbus_message_unref(reply);
}

static void handle_message(DBusConnection *conn, DBusMessage *msg, MprisState *st) {
  if (dbus_message_is_method_call(msg, IFACE_PROPS, "Get")) {
    const char *iface, *prop;
    dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
                          DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    reply_get(conn, msg, st, iface, prop);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PROPS, "GetAll")) {
    const char *iface;
    dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    reply_get_all(conn, msg, st, iface);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PROPS, "Set")) {
    handle_set(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "PlayPause")) {
    cmd_write("PLAYPAUSE");
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "Play")) {
    cmd_write("PLAY");
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "Pause")) {
    cmd_write("PAUSE");
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "Stop")) {
    cmd_write("STOP");
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "Next")) {
    cmd_write("NEXT");
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "Previous")) {
    cmd_write("PREV");
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "Seek")) {
    dbus_int64_t offset;
    dbus_message_get_args(msg, NULL, DBUS_TYPE_INT64, &offset, DBUS_TYPE_INVALID);
    cmd_write_fmt("SEEK:%ld", (long)offset);
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "SetPosition")) {
    const char *track_id;
    dbus_int64_t pos;
    dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &track_id,
                          DBUS_TYPE_INT64, &pos, DBUS_TYPE_INVALID);
    cmd_write_fmt("SETPOS:%ld", (long)pos);
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_PLAYER, "OpenUri")) {
    char *src;
    DBusError err;
    dbus_error_init(&err);
    if (dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &src, DBUS_TYPE_INVALID)) {

      char path[1024];
      snprintf(path, sizeof(path), "%s", src);

      // strip file:// prefix
      char *p = path;
      if (!strncmp(p, "file://", 7)) p += 7;

      // decode %XX in-place
      char *r = p, *w = p;
      while (*r) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
          int hi = (r[1] <= '9') ? r[1]-'0' : (tolower(r[1])-'a'+10);
          int lo = (r[2] <= '9') ? r[2]-'0' : (tolower(r[2])-'a'+10);
          *w++ = (char)((hi << 4) | lo);
          r += 3;
        } else {
          *w++ = *r++;
        }
      }
      *w = '\0';

      cmd_write_fmt("OPEN:%s", p);
      reply_empty(conn, msg);
    } else {
      DBusMessage *error = dbus_message_new_error(msg,
        "org.freedesktop.DBus.Error.InvalidArgs", "Invalid URI argument");
      dbus_connection_send(conn, error, NULL);
      dbus_message_unref(error);
      dbus_error_free(&err);
    }
  }
  else if (dbus_message_is_method_call(msg, IFACE_ROOT, "Raise")) {
    reply_empty(conn, msg);
  }
  else if (dbus_message_is_method_call(msg, IFACE_ROOT, "Quit")) {
    reply_empty(conn, msg);
    raise(SIGINT);
  }
}

// --- public api ---

void mpris_init_dbus(DBusConnection **conn) {
  DBusError err;
  dbus_error_init(&err);

  *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  dbus_err_check(&err, "dbus connect failed");

  int ret = dbus_bus_request_name(*conn, BUS_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
  dbus_err_check(&err, "dbus name request failed");

  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    die("tomu-mpris: another instance is running");
}

void mpris_dispatch(DBusConnection *conn, MprisState *st) {
  dbus_connection_read_write(conn, 500);
  DBusMessage *msg;
  while ((msg = dbus_connection_pop_message(conn)) != NULL) {
    handle_message(conn, msg, st);
    dbus_message_unref(msg);
  }
}

void mpris_notify_change(DBusConnection *conn, MprisState *st) {
  DBusMessage *signal = dbus_message_new_signal(OBJ_PATH, IFACE_PROPS, "PropertiesChanged");
  if (!signal) return;

  DBusMessageIter iter, changed, invalid;
  dbus_message_iter_init_append(signal, &iter);

  const char *iface = IFACE_PLAYER;
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &changed);

  const char *push[] = { "PlaybackStatus", "Metadata", "Volume", "Position", NULL };
  for (int i = 0; push[i]; i++) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(&changed, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &push[i]);
    append_property(st, &entry, push[i]);
    dbus_message_iter_close_container(&changed, &entry);
  }

  dbus_message_iter_close_container(&iter, &changed);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &invalid);
  dbus_message_iter_close_container(&iter, &invalid);

  dbus_connection_send(conn, signal, NULL);
  dbus_message_unref(signal);
}


int main(void) {
  DBusConnection *conn = NULL;
  mpris_init_dbus(&conn);

  MprisState st = {0};
  strncpy(st.data.status, "Stopped", sizeof(st.data.status) - 1);
  fifo_file_reader(&st, 1);

  fprintf(stderr, "tomu-mpris: running\n");

  while (1) {
    state_load(&st);
    mpris_dispatch(conn, &st);
    mpris_notify_change(conn, &st);
    usleep(100000); // 100ms
  }

  return 0;
}
