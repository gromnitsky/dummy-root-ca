#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#ifdef __MINGW64__
#define G_LOG_USE_STRUCTURED 1
#include <libloaderapi.h>
#endif

#include <fcntl.h>

#define G_LOG_DOMAIN "dummy-root-ca"
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "lib.c"
#include "ca.c"

typedef struct GUI {
  GtkBuilder *bld;
  gchar *exe_dir;
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
  GtkFileChooserNative *w = gtk_file_chooser_native_new("Choose directory", parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Choose", "Cancel");
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(w)) == GTK_RESPONSE_ACCEPT) {
    g_autofree gchar *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
    gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(gui.bld, "out")), dir);

    // save it in .ini file
    g_key_file_set_string(gui.ini, "struct", "out", dir);
    g_key_file_save_to_file(gui.ini, gui.inifile, NULL);
  }

  g_object_unref(w);
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
  GtkWidget *btn = GTK_WIDGET(gtk_builder_get_object(gui.bld, "generate"));
  if (gtk_widget_get_sensitive(btn)) // operation is ongoing
    gtk_spinner_start(w);
  else
    gtk_spinner_stop(w);
}

void generate_button_toggle() {
  GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(gui.bld, "generate"));
  gtk_widget_set_sensitive(w, !gtk_widget_is_sensitive(w));
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

void generator_finish(GObject *_unused, GAsyncResult *res, gpointer user_data) {
  GenOpt *opt = user_data;
  g_debug("generate_finish: %s", opt->cn);
  genopt_free(&opt);

  GError *err = g_task_propagate_pointer(G_TASK(res), NULL);
  g_debug("generate_finish: error = %s", err ? err->message : "none");

  info(err ? err->message : "Done.");

  if (err) g_error_free(err);
  spinner();
  generate_button_toggle();
}

void generator_in_thread(GTask *task, gpointer _source_object,
                         gpointer task_data, GCancellable *_cancellable) {
  g_debug("generate_in_thread");
  GenOpt *opt = task_data;
  GError *err = NULL; // frees `err` in generate_finish()
  mk_keys_and_certs(opt->out, opt->cn, opt->altname, atoi(opt->key_size),
                    opt->days, opt->overwrite_all, &err);
  g_task_return_pointer(task, err, NULL);
}

void on_generate_clicked() {
  GenOpt *opt = generator_options();

  if (!strlen(opt->cn)) {
    info("CommonName is empty or invalid");
    genopt_free(&opt);
    return;
  }

  if (-1 == g_mkdir_with_parents(opt->out, 0775)) {
    info(strerror(errno));
    return;
  }

  info("Processing...");
  spinner();
  generate_button_toggle();

  g_debug("out: `%s`, days: `%d`, key size: `%s`, CN: `%s`, subjectAltName: `%s` [%d], overwrite all: %d", opt->out, opt->days, opt->key_size, opt->cn, opt->altname, (int)strlen(opt->altname), opt->overwrite_all);

  // uncancellable, frees `opt` in generate_finish()
  GTask *task = g_task_new(NULL, NULL, generator_finish, opt);
  g_task_set_task_data(task, opt, NULL);
  g_task_run_in_thread(task, generator_in_thread);
  g_object_unref(task);
}

int main(int argc, char **argv) {
  putenv("GTK_CSD=0");          /* how do I set this via Glade? */

  // state file
  gui.inifile = g_build_filename(g_get_user_state_dir(),
                                 G_LOG_DOMAIN, "genopt.ini", NULL);
  g_autofree gchar *ini_dir = g_path_get_dirname(gui.inifile);
  g_mkdir_with_parents(ini_dir, 0775);
  gui.ini = g_key_file_new();

#ifdef __MINGW64__
  // log to a file, instead of stderr
  g_autofree gchar *logfile = g_build_filename(ini_dir, "log.txt", NULL);
  int logfile_fd = open(logfile, O_CREAT|O_WRONLY|O_TRUNC, 0644);
  g_log_set_writer_func(my_log_writer, &logfile_fd, NULL);
#endif

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

  // load from the state file
  g_debug("inifile: %s", gui.inifile);
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

  gtk_widget_show(toplevel);
  gtk_main();
  return 0;
}
