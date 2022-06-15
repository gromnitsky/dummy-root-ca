#ifdef __MINGW64__
#include <libloaderapi.h>
#endif

#define G_LOG_DOMAIN "dummy-root-ca"
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "lib.c"

typedef struct GUI {
  GtkBuilder *bld;
  gchar *exe_dir;
  GSubprocess *cmd;
  GCancellable *cmd_ca;
  gchar *inifile;
  GKeyFile* ini;
} GUI;

GUI gui;

void on_files_selection_changed(GtkTreeSelection *w) { /* TODO: remove */
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

void on_out_button_clicked() {
  GtkWindow *parent = GTK_WINDOW(gtk_builder_get_object(gui.bld, "toplevel"));
  GtkWidget *w = gtk_file_chooser_dialog_new(NULL, parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, NULL);
  if (gtk_dialog_run(GTK_DIALOG(w)) == GTK_RESPONSE_ACCEPT) {
    g_autofree char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
    gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "out")), dir);

    // save it in .ini file
    g_key_file_set_string(gui.ini, "struct", "out", dir);
    g_key_file_save_to_file(gui.ini, gui.inifile, NULL);
  }

  gtk_widget_destroy(w);
}

void on_CN_changed(GtkEntry *w) {
  const gchar *cn_orig = gtk_entry_get_text(w);
  gchar *cn = g_strdup(cn_orig);

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

void genopt_free(GenOpt **v) {
  g_free((*v)->key_size);
  g_free((*v)->altname);
  g_free(*v);
  *v = NULL;
}

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

void info(gchar *msg) {
  GtkEntryBuffer *w = GTK_ENTRY_BUFFER(gtk_builder_get_object(gui.bld, "log"));
  gtk_entry_buffer_set_text(w, msg, -1);
}

void spinner() {
  GtkSpinner *w = GTK_SPINNER(gtk_builder_get_object(gui.bld, "spinner"));
  if (NULL != g_subprocess_get_identifier(gui.cmd)) // process isn't dead
    gtk_spinner_start(w);
  else
    gtk_spinner_stop(w);
}

void generate_button_label_toggle() {
  GtkButton *w = GTK_BUTTON(gtk_builder_get_object(gui.bld, "generate"));
  gchar *t = (0 == strcmp(gtk_button_get_label(w), "Abort")) ? "Generate" : "Abort";
  gtk_button_set_label(w, t);
}

GenOpt* generator_options() {
  GenOpt* opt = (GenOpt*)g_malloc(1*sizeof(GenOpt));

  opt->out = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "out")));
  opt->days = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(gui.bld, "days")));
  opt->key_size = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gtk_builder_get_object(gui.bld, "key_size")));

  opt->cn = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "CN")));
  if (!is_valid_domain(opt->cn)) opt->cn = "";

  opt->altname = altname(gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "subjectAltName"))));

  opt->overwrite_all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(gui.bld, "overwrite1")));

  return opt;
}

void cmd_callback(GObject *_source_object, GAsyncResult *_res, gpointer _data) {
  spinner();
  gint exit_code = g_subprocess_get_exit_status(gui.cmd);
  gboolean cancelled = g_cancellable_is_cancelled(gui.cmd_ca);
  g_debug("Make exit code: %d", exit_code);
  g_debug("Make cancelled: %d", cancelled);

  if (cancelled) {
    info("Interrupted by user");
  } else if (exit_code != 0) {
    info("Operation failed, see stderr");
  } else {
    info("Done.");
  }

  generate_button_label_toggle();
}

void on_generate_clicked() {
  if (NULL != g_subprocess_get_identifier(gui.cmd)) { // process isn't dead
    g_debug("abort Make");
#ifdef __MINGW64__
    g_subprocess_force_exit(gui.cmd); /* SIGKILL */
#else
    g_subprocess_send_signal(gui.cmd, 2); /* SIGINT */
#endif
    g_cancellable_cancel(gui.cmd_ca);
    return;
  }

  GenOpt *opt = generator_options();

  if (!strlen(opt->cn)) {
    info("CommonName is empty or invalid");
    genopt_free(&opt);
    return;
  }

  g_debug("out: `%s`, days: `%d`, key size: `%s`, CN: `%s`, subjectAltName: `%s` [%d], overwrite all: %d", opt->out, opt->days, opt->key_size, opt->cn, opt->altname, (int)strlen(opt->altname), opt->overwrite_all);

  char server_cert[BUFSIZ];
  snprintf(server_cert, sizeof server_cert, "%s/%s.crt", opt->out, opt->cn);
  g_debug("unlinking %s", server_cert);
  if (!opt->overwrite_all) g_unlink(server_cert);

  char target[BUFSIZ];
  snprintf(target, sizeof target, "%s.crt", opt->cn);

  g_autofree gchar *make = g_find_program_in_path("make");

  gchar makefile[PATH_MAX];
  snprintf(makefile, sizeof makefile, "%s/dummy-root-ca.mk", gui.exe_dir);

  gchar d[BUFSIZ];
  snprintf(d, sizeof d, "d=%d", opt->days);

  gchar key_size[BUFSIZ];
  snprintf(key_size, sizeof key_size, "key_size=%s", opt->key_size);

  gchar tls_altname[BUFSIZ];
  snprintf(tls_altname, sizeof tls_altname, "tls.altname=%s", opt->altname);

  gchar openssl[BUFSIZ];
  g_autofree gchar *openssl_cmd = g_find_program_in_path("openssl");
  snprintf(openssl, sizeof openssl, "openssl=%s", openssl_cmd);

  g_autoptr(GError) err = NULL;
  g_cancellable_reset(gui.cmd_ca);
  g_mkdir_with_parents(opt->out, 0775);

  GSubprocessLauncher *lc = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_set_cwd(lc, opt->out);
  gui.cmd = g_subprocess_launcher_spawn(lc, &err,
                                        make, opt->overwrite_all ? "-B" : "-b",
                                        "-f", makefile, d, key_size,
                                        tls_altname, openssl, target,
                                        NULL);
  genopt_free(&opt);
  g_object_unref(lc);
  if (err) {
    info(err->message);
    return;
  }

  g_subprocess_wait_async(gui.cmd, gui.cmd_ca, cmd_callback, NULL);

  info("Processing...");
  spinner();
  generate_button_label_toggle();
}

int main(int argc, char **argv) {
  putenv("GTK_CSD=0");          /* how do I set this via Glade? */

  GError *error = NULL;
  if (!gtk_init_with_args(&argc, &argv, "output_dir", NULL, NULL, &error))
    g_error("%s", error->message);

  gui.exe_dir = exe_dir();
  g_debug("exe_dir = %s", gui.exe_dir);

  char buf[PATH_MAX];
  snprintf(buf, sizeof buf, "%s/gui.xml", gui.exe_dir);
  gui.bld = gtk_builder_new_from_file(buf);

  GtkWidget *toplevel = GTK_WIDGET(gtk_builder_get_object(gui.bld, "toplevel"));
  gtk_window_set_title(GTK_WINDOW(toplevel), "Dummy Root CA");
  g_signal_connect(toplevel, "destroy", gtk_main_quit, NULL);
  gtk_builder_connect_signals(gui.bld, NULL);

  GtkEntry *out = GTK_ENTRY(gtk_builder_get_object(gui.bld, "out"));

  // load state file
  gui.inifile = g_build_filename(g_get_user_state_dir(),
                                 G_LOG_DOMAIN, "genopt.ini", NULL);
  g_debug("inifile: %s", gui.inifile);
  g_autofree gchar *ini_dir = g_path_get_dirname(gui.inifile);
  g_mkdir_with_parents(ini_dir, 0775);
  gui.ini = g_key_file_new();
  if (g_key_file_load_from_file(gui.ini, gui.inifile, G_KEY_FILE_NONE, NULL)) {
    g_autofree gchar *v = g_key_file_get_string(gui.ini, "struct", "out", NULL);
    if (v) gtk_entry_set_text(out, v);
  }

  // set 'out'
  if (argc > 1) {
    gtk_entry_set_text(out, argv[1]);
  } else {
    if (0 == strlen(gtk_entry_get_text(out)))
      gtk_entry_set_text(out, getcwd(buf, sizeof buf));
  }

  // toggle buttons & focus
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(gui.bld, "overwrite2")), TRUE);
  gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(gui.bld, "generate")));

  // load CSS
  GtkCssProvider *cssProvider = gtk_css_provider_new();
  snprintf(buf, sizeof buf, "%s/style.css", gui.exe_dir);
  if (!gtk_css_provider_load_from_path(cssProvider, buf, &error))
    g_error("%s", error->message);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);

  // for 'abort' button
  gui.cmd_ca = g_cancellable_new();

  gtk_widget_show(toplevel);
  gtk_main();
  return 0;
}
