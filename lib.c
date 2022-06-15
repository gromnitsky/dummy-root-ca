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
