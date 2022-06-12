#include <gtk/gtk.h>

GtkBuilder *builder;

void on_files_selection_changed(GtkTreeSelection *w) {
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *val;

  if (!gtk_tree_selection_get_selected(w, &model, &iter)) return;

  gtk_tree_model_get(model, &iter, 2, &val, -1);
  fprintf(stderr, "TODO: display the parsed cert %s\n", val);
  g_free(val);
  //gtk_tree_model_unref_node(model, &iter);
  gtk_tree_selection_unselect_all(w);
}

void quit() {
  g_object_unref(G_OBJECT(builder)); // make valgrind happy
  gtk_main_quit();
}

void on_out_button_clicked() {
  GtkWindow *parent = GTK_WINDOW(gtk_builder_get_object(builder, "toplevel"));
  GtkWidget *w = gtk_file_chooser_dialog_new(NULL, parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, NULL);
  if (gtk_dialog_run(GTK_DIALOG(w)) == GTK_RESPONSE_ACCEPT) {
    char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
    gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "out")), dir);
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
  GtkTreeView *tv = GTK_TREE_VIEW(gtk_builder_get_object(builder, "files"));
  GtkListStore *ls = GTK_LIST_STORE(gtk_builder_get_object(builder, "files_ls"));
  GtkTreeModel *model = gtk_tree_view_get_model(tv);
  GtkTreeIter i;
  char fname[BUFSIZ];

  gtk_tree_model_get_iter_from_string(model, &i, "1");
  snprintf(fname, sizeof fname, (strlen(cn) ? "%s.pem" : "%s-"), cn);
  gtk_list_store_set(ls, &i, 1, fname, -1);
  snprintf(fname, sizeof fname, (strlen(cn) ? "%s.crt" : "%s-"), cn);
  gtk_list_store_set(ls, &i, 2, fname, -1);
  free(cn);
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

void on_gen_close_clicked(GtkButton *w) {
  fprintf(stderr, "on_gen_close_clicked\n");
  GtkWidget *gen = GTK_WIDGET(gtk_builder_get_object(builder, "gen"));
  gtk_widget_hide(gen);
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
  char exe_path[PATH_MAX];
  ssize_t nbytes = readlink("/proc/self/exe", exe_path, PATH_MAX);
  exe_path[nbytes] = '\0';
  return g_path_get_dirname(exe_path);
}

int run_make(gchar *target, GenOpt *opt) {
  char cmd[BUFSIZ];
  char *dir = exe_dir();

  snprintf(cmd, sizeof cmd, "%s/dummy-root-ca.mk %s out=%s d=%d key_size=%s tls.altname=%s", dir, target, opt->out, opt->days, opt->key_size, opt->altname);
  g_free(dir);

  return system(cmd);
}

void* generator(void *arg) {    /* thread function */
  GenOpt *opt = (GenOpt*)arg;
  GtkEntryBuffer *log = GTK_ENTRY_BUFFER(gtk_builder_get_object(builder, "log"));
  GtkProgressBar *progress = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, "progress"));
  char target[BUFSIZ];
  void *r = (void*)1;

  gtk_entry_buffer_set_text(log, "① Root private key...", -1);
  if (0 != run_make("root.pem", opt)) {
    gtk_entry_buffer_set_text(log, "Error: making root private key failed", -1);
    r = (void*)0;
    goto generator_cleanup;
  }
  gtk_progress_bar_set_fraction(progress, 0.25);

  gtk_entry_buffer_set_text(log, "② Server private key...", -1);
  snprintf(target, sizeof target, "%s.pem", opt->cn);
  if (0 != run_make(target, opt)) {
    gtk_entry_buffer_set_text(log, "Error: making server private key failed", -1);
    r = (void*)0;
    goto generator_cleanup;
  }
  gtk_progress_bar_set_fraction(progress, 0.50);

  gtk_entry_buffer_set_text(log, "③ Root certificate...", -1);
  if (0 != run_make("root.crt", opt)) {
    gtk_entry_buffer_set_text(log, "Error: making root certificate failed", -1);
    r = (void*)0;
    goto generator_cleanup;
  }
  gtk_progress_bar_set_fraction(progress, 0.75);

  gtk_entry_buffer_set_text(log, "④ Server certificate...", -1);
  snprintf(target, sizeof target, "%s.crt", opt->cn);
  if (0 != run_make(target, opt)) {
    gtk_entry_buffer_set_text(log, "Error: making server certificate failed", -1);
    r = (void*)0;
    goto generator_cleanup;
  }
  gtk_progress_bar_set_fraction(progress, 0);

  gtk_entry_buffer_set_text(log, "Done.", -1);

 generator_cleanup:
  g_free(opt->altname);
  g_free(opt);

  return r;
}

void generate(const gchar *out, gint days, gchar *key_size,
                    const gchar *cn, gchar **altname, gboolean overwrite_all) {
  fprintf(stderr, "out: %s\n", out);
  fprintf(stderr, "days: %d\n", days);
  fprintf(stderr, "key size: %s\n", key_size);
  fprintf(stderr, "CN: %s\n", cn);

  gchar *an = g_strjoinv(",", altname);
  fprintf(stderr, "subjectAltName: `%s` [%ld]\n", an, strlen(an));

  fprintf(stderr, "overwrite all: %d\n", overwrite_all);

  GtkEntryBuffer *log = GTK_ENTRY_BUFFER(gtk_builder_get_object(builder, "log"));
  if (strlen(cn)) {
    GenOpt *opt = (GenOpt*)g_malloc(1*sizeof(GenOpt));
    opt->out = out;
    opt->key_size = key_size;
    opt->days = days;
    opt->cn = cn;
    opt->altname = strdup(an);
    opt->overwrite_all = overwrite_all;

    pthread_t tid;
    pthread_create(&tid, NULL, generator, opt);
  } else {
    gtk_entry_buffer_set_text(log, "Error: CommonName is empty or invalid", -1);
  }

  g_free(an);
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
  const gchar *out = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(builder, "out")));
  gint days = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "days")));
  gchar *key_size = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "key_size")));

  const gchar *cn = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(builder, "CN")));
  if (!is_valid_domain(cn)) cn = "";

  const gchar *altname_orig = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(builder, "subjectAltName")));
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

  gboolean overwrite_all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "overwrite1")));


  generate(out, days, key_size, cn, altname, overwrite_all);

  // cleanup
  g_free(key_size);
  for (gchar **p = altname; *p; p++) g_free(*p);
}


int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    builder = gtk_builder_new();
    GError *error = NULL;
    if (!gtk_builder_add_from_file(builder, "gui.xml", &error)) {
      GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      exit(1);
    }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "toplevel"));
    g_signal_connect(window, "destroy", quit, NULL);
    gtk_builder_connect_signals(builder, NULL);

    char out[PATH_MAX];
    getcwd(out, sizeof out);
    gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(builder, "out")), out);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "overwrite2")), TRUE);

    gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(builder, "generate")));

    // load CSS
    GtkCssProvider *cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(cssProvider, "style.css", NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_widget_show(window);
    gtk_main();
    return 0;
}
