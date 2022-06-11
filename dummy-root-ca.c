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

void on_CN_changed(GtkEntry *w) {
  const gchar *cn = gtk_entry_get_text(w);

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
}

void on_generate_clicked() {
  fprintf(stderr, "on_generate_clicked\n");

  const gchar *out = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(builder, "out")));
  fprintf(stderr, "out: %s\n", out);

  gint days = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "days")));
  fprintf(stderr, "days: %d\n", days);

  gchar *key_size = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "key_size")));
  fprintf(stderr, "key size: %s\n", key_size);

  const gchar *cn = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(builder, "CN")));
  fprintf(stderr, "CN: %s\n", cn);

  const gchar *altname = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(builder, "subjectAltName")));
  fprintf(stderr, "subjectAltName: %s\n", altname);

  gboolean overwrite_all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "overwrite1")));
  fprintf(stderr, "overwrite all: %d\n", overwrite_all);
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

    gtk_widget_show(window);
    gtk_main();
    return 0;
}
