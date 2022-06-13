#ifdef __MINGW64__
#include <libloaderapi.h>
#endif
#include <pthread.h>

#define G_LOG_DOMAIN "dummy-root-ca"
#include <glib/gstdio.h>
#include <gtk/gtk.h>

typedef struct GUI {
  GtkBuilder *bld;
  pthread_t generator_tid;      /* starts after 'Generate' button ckick */
  gboolean generator_active;
  gchar *exe_dir;
} GUI;

GUI gui;

void on_files_selection_changed(GtkTreeSelection *w) {
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *val;

  if (!gtk_tree_selection_get_selected(w, &model, &iter)) return;

  gtk_tree_model_get(model, &iter, 2, &val, -1);
  g_message("TODO: display the parsed cert %s", val);
  g_free(val);
  //gtk_tree_model_unref_node(model, &iter);
  gtk_tree_selection_unselect_all(w);
}

void quit() {
  g_object_unref(G_OBJECT(gui.bld)); // make valgrind happy
  g_free(gui.exe_dir);
  gtk_main_quit();
}

void on_out_button_clicked() {
  GtkWindow *parent = GTK_WINDOW(gtk_builder_get_object(gui.bld, "toplevel"));
  GtkWidget *w = gtk_file_chooser_dialog_new(NULL, parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, NULL);
  if (gtk_dialog_run(GTK_DIALOG(w)) == GTK_RESPONSE_ACCEPT) {
    char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
    gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "out")), dir);
    g_free(dir);
  }

  gtk_widget_destroy(w);
}

gboolean is_valid_domain(const gchar *cn) {
  return g_regex_match_simple("(?=^.{1,253}$)(^(((?!-)[a-zA-Z0-9-]{1,63}(?<!-))|((?!-)[a-zA-Z0-9-]{1,63}(?<!-)\\.)+[a-zA-Z]{2,63})$)", cn, 0, 0);
}

gboolean is_valid_ip4(const gchar *ip) {
  return g_regex_match_simple("^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$", ip, 0, 0);
}

void on_CN_changed(GtkEntry *w) {
  const gchar *cn_orig = gtk_entry_get_text(w);
  gchar *cn = strdup(cn_orig);

  GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(w));
  if (is_valid_domain(cn)) {
    gtk_style_context_remove_class(ctx, "invalid_entry");
  } else {
    strcpy(cn, "");
    gtk_style_context_add_class(ctx, "invalid_entry");
  }

  // insert CN into 'files' table
  GtkTreeView *tv = GTK_TREE_VIEW(gtk_builder_get_object(gui.bld, "files"));
  GtkListStore *ls = GTK_LIST_STORE(gtk_builder_get_object(gui.bld, "files_ls"));
  GtkTreeModel *model = gtk_tree_view_get_model(tv);
  GtkTreeIter i;
  char fname[BUFSIZ];

  gtk_tree_model_get_iter_from_string(model, &i, "1");
  snprintf(fname, sizeof fname, (strlen(cn) ? "%s.pem" : "%s-"), cn);
  gtk_list_store_set(ls, &i, 1, fname, -1);
  snprintf(fname, sizeof fname, (strlen(cn) ? "%s.crt" : "%s-"), cn);
  gtk_list_store_set(ls, &i, 2, fname, -1);
  g_free(cn);
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

void on_subjectAltName_changed(GtkEntry *w) {
  const gchar *altname = gtk_entry_get_text(w);
  GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(w));
  if (is_valid_altname(altname)) {
    gtk_style_context_remove_class(ctx, "invalid_entry");
  } else {
    gtk_style_context_add_class(ctx, "invalid_entry");
  }
}

typedef struct GenOpt {
  const gchar *out;
  gint days;
  gchar *key_size;
  const gchar *cn;
  gchar *altname;
  gboolean overwrite_all;
} GenOpt;

gchar* exe_dir() {
#ifdef __MINGW64__
  wchar_t filename[MAX_PATH] = { 0 };
  GetModuleFileNameW(NULL, filename, MAX_PATH);
  gchar *path = g_utf16_to_utf8(filename, -1, NULL, NULL, NULL);
  gchar *r = g_path_get_dirname(path);
  g_free(path);
  return r;
#else
  char exe_path[PATH_MAX];
  ssize_t nbytes = readlink("/proc/self/exe", exe_path, PATH_MAX);
  exe_path[nbytes] = '\0';
  return g_path_get_dirname(exe_path);
#endif
}

GError* run_make(gchar *target, GenOpt *opt) {
  char buf[BUFSIZ];
  snprintf(buf, sizeof buf, "%s/%s", opt->out, target);
  if (opt->overwrite_all) g_unlink(buf);

  g_mkdir_with_parents(opt->out, 0775);

  gchar *make = g_find_program_in_path("make");
  gchar makefile[PATH_MAX];
  snprintf(makefile, sizeof makefile, "%s/dummy-root-ca.mk", gui.exe_dir);
  gchar d[BUFSIZ];
  snprintf(d, sizeof d, "d=%d", opt->days);
  gchar key_size[BUFSIZ];
  snprintf(key_size, sizeof key_size, "key_size=%s", opt->key_size);
  gchar tls_altname[BUFSIZ];
  snprintf(tls_altname, sizeof tls_altname, "tls.altname=%s", opt->altname);
  gchar openssl_param[BUFSIZ];
  gchar *openssl = g_find_program_in_path("openssl");
  snprintf(openssl_param, sizeof openssl_param, "openssl=%s", openssl);

  gchar *args[] = { make, "-f", makefile, d, key_size, tls_altname,
                    openssl_param, target, NULL };
  gint wait_status;
  GError *err = NULL;
  if (g_spawn_sync(opt->out, args, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
                   NULL, &wait_status,&err)) {
    g_spawn_check_wait_status(wait_status, &err);
  }

  g_free(make);
  g_free(openssl);
  return err;
}

gboolean idle_generate_button_toggle(gpointer _unused) {
  GtkButton *w = GTK_BUTTON(gtk_builder_get_object(gui.bld, "generate"));
  if (0 == strcmp(gtk_button_get_label(w), "Generate"))
    gtk_button_set_label(w, "Abort");
  else
    gtk_button_set_label(w, "Generate");
  return FALSE;
}

void generator_cleanup(void *arg) {
  g_debug("generator_cleanup()");
  GenOpt *opt = (GenOpt*)arg;
  g_free(opt->key_size);
  g_free(opt->altname);
  g_free(opt);

  g_idle_add(idle_generate_button_toggle, NULL);
  gui.generator_active = FALSE; /* mark thread as finished */
}

void generator_log(GtkEntryBuffer *sink, gchar *msg, GError **err) {
  char buf[BUFSIZ];
  snprintf(buf, sizeof buf, "Error: %s: %s", msg, (*err)->message);
  gtk_entry_buffer_set_text(sink, buf, -1);
  g_error_free(*err);
}

void* generator(void *arg) {    /* thread function */
  GenOpt *opt = (GenOpt*)arg;
  gui.generator_active = TRUE;
#ifndef __MINGW64__
  pthread_cleanup_push(generator_cleanup, opt);
#endif
  g_idle_add(idle_generate_button_toggle, NULL);

  GtkProgressBar *progress = GTK_PROGRESS_BAR(gtk_builder_get_object(gui.bld, "progress"));
  GtkEntryBuffer *log = GTK_ENTRY_BUFFER(gtk_builder_get_object(gui.bld, "log"));
  char target[PATH_MAX];
  GError *err;

  gtk_entry_buffer_set_text(log, "① Root private key...", -1);
  if ( (err = run_make("root.pem", opt))) {
    generator_log(log, "making root private key failed", &err);
#ifdef __MINGW64__
    generator_cleanup(opt);
#endif
    pthread_exit((void*)0);
  }
  gtk_progress_bar_set_fraction(progress, 0.25);

  gtk_entry_buffer_set_text(log, "② Server private key...", -1);
  snprintf(target, sizeof target, "%s.pem", opt->cn);
  if ( (err = run_make(target, opt))) {
    generator_log(log, "making server private key failed", &err);
#ifdef __MINGW64__
    generator_cleanup(opt);
#endif
    pthread_exit((void*)0);
  }
  gtk_progress_bar_set_fraction(progress, 0.5);

  gtk_entry_buffer_set_text(log, "③ Root certificate...", -1);
  if ( (err = run_make("root.crt", opt))) {
    generator_log(log, "making root certificate failed", &err);
#ifdef __MINGW64__
    generator_cleanup(opt);
#endif
    pthread_exit((void*)0);
  }
  gtk_progress_bar_set_fraction(progress, 0.75);

  gtk_entry_buffer_set_text(log, "④ Server certificate...", -1);
  snprintf(target, sizeof target, "%s.crt", opt->cn);
  if (!opt->overwrite_all) {
    char old[BUFSIZ];
    snprintf(old, sizeof old, "%s/%s", opt->out, target);
    g_unlink(old);
  }
  if ( (err = run_make(target, opt))) {
    generator_log(log, "making server certificate failed", &err);
#ifdef __MINGW64__
    generator_cleanup(opt);
#endif
    pthread_exit((void*)0);
  }
  gtk_progress_bar_set_fraction(progress, 0);

  gtk_entry_buffer_set_text(log, "Done.", -1);
#ifndef __MINGW64__
  pthread_cleanup_pop(1);
#else
  generator_cleanup(opt);
#endif
  return (void*)1;
}

void generate(const gchar *out, gint days, gchar *key_size,
                    const gchar *cn, gchar *altname, gboolean overwrite_all) {
  g_debug("out: `%s`, days: `%d`, key size: `%s`, CN: `%s`, subjectAltName: `%s` [%d], overwrite all: %d", out, days, key_size, cn, altname, (int)strlen(altname), overwrite_all);

  if (!strlen(cn)) {
    GtkEntryBuffer *log = GTK_ENTRY_BUFFER(gtk_builder_get_object(gui.bld, "log"));
    gtk_entry_buffer_set_text(log, "Error: CommonName is empty or invalid", -1);
    return;
  }

  GenOpt *opt = (GenOpt*)g_malloc(1*sizeof(GenOpt));
  opt->out = out;
  opt->key_size = strdup(key_size);
  opt->days = days;
  opt->cn = cn;
  opt->altname = strdup(altname);
  opt->overwrite_all = overwrite_all;

  pthread_create(&gui.generator_tid, NULL, generator, opt);
}

int altname_get_theoretical_size(const gchar *altname_orig) {
  int size = 0;
  gchar **list = g_regex_split_simple(",", altname_orig, 0, 0);
  for (gchar **p = list; *p; p++) {
    g_strstrip(*p); if (!strlen(*p)) continue;
    size++;
  }
  g_strfreev(list);
  return size;
}

void on_generate_clicked() {
  if (gui.generator_active && 0 == pthread_cancel(gui.generator_tid)) {
    g_debug("cancel thread");
    return;
  }

  const gchar *out = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "out")));
  gint days = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(gui.bld, "days")));
  gchar *key_size = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gtk_builder_get_object(gui.bld, "key_size")));

  const gchar *cn = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "CN")));
  if (!is_valid_domain(cn)) cn = "";

  const gchar *altname_orig = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "subjectAltName")));
  int altname_size = altname_get_theoretical_size(altname_orig);
  gchar *altname[altname_size+1]; // ["DNS:example.com", "IP:127.0.0.1", NULL]

  if (is_valid_altname(altname_orig)) {
    int idx = 0;
    gchar **list = g_regex_split_simple(",", altname_orig, 0, 0);
    for (gchar **p = list; *p; p++) {
      g_strstrip(*p); if (!strlen(*p)) continue;

      int len = strlen(*p)+10;
      gchar *val = g_malloc(len);
      snprintf(val, len, (is_valid_domain(*p) ? "DNS:%s" : "IP:%s"), *p);
      altname[idx++] = val;
    }
    altname[altname_size] = NULL;
    g_strfreev(list);
  } else {
    altname[0] = NULL;
  }
  gchar *an = g_strjoinv(",", altname);

  gboolean overwrite_all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(gui.bld, "overwrite1")));

  // starts a new thread
  generate(out, days, key_size, cn, an, overwrite_all);

  // cleanup
  g_free(key_size);
  g_free(an);
  for (gchar **p = altname; *p; p++) g_free(*p);
}

int main(int argc, char **argv) {
  GError *error = NULL;
  if (!gtk_init_with_args(&argc, &argv, "output_dir", NULL, NULL, &error))
    g_error("%s", error->message);

  char path[BUFSIZ];
  gui.exe_dir = exe_dir();
  g_debug("exe_dir = %s", gui.exe_dir);

  gui.bld = gtk_builder_new();
  snprintf(path, sizeof path, "%s/gui.xml", gui.exe_dir);
  if (!gtk_builder_add_from_file(gui.bld, path, &error))
    g_error("%s", error->message);

  GtkWidget *toplevel = GTK_WIDGET(gtk_builder_get_object(gui.bld, "toplevel"));
  g_signal_connect(toplevel, "destroy", quit, NULL);
  gtk_builder_connect_signals(gui.bld, NULL);

  // set 'out' directory
  char out[PATH_MAX];
  argc > 1 ? strcpy(out, argv[1]) : getcwd(out, sizeof out);
  gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "out")), out);

  // toggle buttons & focus
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(gui.bld, "overwrite2")), TRUE);
  gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(gui.bld, "generate")));

  // load CSS
  GtkCssProvider *cssProvider = gtk_css_provider_new();
  snprintf(path, sizeof path, "%s/style.css", gui.exe_dir);
  if (!gtk_css_provider_load_from_path(cssProvider, path, &error))
    g_error("%s", error->message);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);

  gtk_widget_show(toplevel);
  gtk_main();
  return 0;
}
