gboolean is_valid_domain(const gchar *cn) {
  return g_regex_match_simple("(?=^.{1,253}$)(^(((?!-)[a-zA-Z0-9-]{1,63}(?<!-))|((?!-)[a-zA-Z0-9-]{1,63}(?<!-)\\.)+[a-zA-Z]{2,63})$)", cn, 0, 0);
}

gboolean is_valid_ip4(const gchar *ip) {
  return g_regex_match_simple("^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$", ip, 0, 0);
}

gboolean is_valid_altname(const gchar *altname) { // empty altname is OK
  gboolean r = TRUE;
  gchar **list = g_regex_split_simple(",", altname, 0, 0);
  for (gchar **p = list; *p; p++) {
    g_strstrip(*p); if (!strlen(*p)) continue;

    if ( !(is_valid_domain(*p) || is_valid_ip4(*p))) {
      r = FALSE;
      break;
    }
  }
  g_strfreev(list);
  return r;
}

gchar* altname(const gchar *text) {
  g_autoptr(GString) r = g_string_new("");
  gchar **list = g_regex_split_simple(",", text, 0, 0);
  for (gchar **p = list; *p; p++) {
      g_strstrip(*p); if (!strlen(*p)) continue;
      g_string_append_printf(r, (is_valid_domain(*p) ? "%sDNS:%s" : "%sIP:%s"),
                             (r->str[0] ? "," : ""), *p);
  }
  g_strfreev(list);
  return g_strdup(r->str);
}

// a custom log writer for Windows
GLogWriterOutput my_log_writer(GLogLevelFlags log_level,
                               const GLogField *fields,
                               gsize n_fields,
                               gpointer user_data) {
  if (g_log_writer_default_would_drop(log_level, G_LOG_DOMAIN))
    return G_LOG_WRITER_HANDLED;

  int *fd = user_data;
  if (!fd || *fd == -1) return g_log_writer_standard_streams(log_level, fields, n_fields, user_data);

  const char *file = NULL;
  const char *line = NULL;
  const char *func = NULL;
  const char *message = NULL;

  for (const GLogField* p = fields; n_fields > 0; n_fields--, p++) {
    if (p->length != -1) continue;
    if (0 == strcmp("GLIB_DOMAIN", p->key)
        && 0 != strcmp(p->value, G_LOG_DOMAIN))
      return G_LOG_WRITER_HANDLED;

    if (0 == strcmp("CODE_FILE", p->key)) file = p->value;
    if (0 == strcmp("CODE_LINE", p->key)) line = p->value;
    if (0 == strcmp("CODE_FUNC", p->key)) func = p->value;
    if (0 == strcmp("MESSAGE", p->key)) message = p->value;
  }

  char time_buf[100];
  gint64 now = g_get_real_time();
  time_t now_secs = now / 1000000;
  struct tm now_tm = {0}; localtime_r(&now_secs, &now_tm);
  strftime(time_buf, sizeof time_buf, "%H:%M:%S", &now_tm);

  char buf[BUFSIZ];
  int bufsize = snprintf(buf, sizeof buf, "%s.%03d %03d %s:%s:%s: %s\n",
                         time_buf, (int)((now / 1000) % 1000),
                         log_level, file, line, func, message);
  if (-1 == write(*fd, buf, bufsize))
    return g_log_writer_standard_streams(log_level, fields, n_fields, user_data);

  return G_LOG_WRITER_HANDLED;
}
