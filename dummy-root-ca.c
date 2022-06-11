#include <gtk/gtk.h>

void files_on_select_changed(GtkWidget *widget) {
  GtkTreeSelection *w = GTK_TREE_SELECTION(widget);
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *val;

  if (!gtk_tree_selection_get_selected(w, &model, &iter)) return;

  gtk_tree_model_get(model, &iter, 2, &val, -1);
  printf("TODO: display the parsed cert %s\n", val);
  g_free(val);
  //gtk_tree_model_unref_node(model, &iter);
  gtk_tree_selection_unselect_all(w);
}

void files_init(GtkBuilder *bld) {
  GtkTreeStore *ts = GTK_TREE_STORE(gtk_builder_get_object(bld, "files_ts"));
  GtkTreeIter i;
  gtk_tree_store_append(ts, &i, NULL);
  gtk_tree_store_set(ts, &i, 0, "Root", -1);
  gtk_tree_store_set(ts, &i, 1, "root.pem", -1);
  gtk_tree_store_set(ts, &i, 2, "root.crt", -1);
  gtk_tree_store_append(ts, &i, NULL);
  gtk_tree_store_set(ts, &i, 0, "Server", -1);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;
    if (!gtk_builder_add_from_file(builder, "gui.xml", &error)) {
      GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      exit(1);
    }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "toplevel"));
    g_signal_connect(window, "destroy", gtk_main_quit, NULL);
    gtk_builder_connect_signals(builder, NULL);

    files_init(builder);

    g_object_unref(G_OBJECT(builder));
    gtk_widget_show(window);
    gtk_main();
    return 0;
}
