//######################################################################################################
//
// > geanyplugingsantnerutils: Plugin for Geany editor (https://github.com/geany/geany)
//
// Maintained by Gregor Santner, 2019-
// https://gsantner.net/
//
// License: Apache 2.0 / Commercial (Dual)
//
//######################################################################################################
//
// Notes: 
//
// while [ 1 -eq 1 ] ; do make install && geany -v && sleep 0.2; done
// while [ 1 -eq 1 ] ; do make install && GTK_DEBUG=interactive geany -v && sleep 0.2; done
//
// msgwin_status_add ("[%s] AFTER C", label);
//
//######################################################################################################
// vim: sw=4 ts=4 ft=c noexpandtab:


// Includes
#include "geanyplugin.h"
#include <stdio.h>


// Plugin setup
const char *PLUGIN_NAME = "geanygsantnerutils";
GeanyPlugin *geany_plugin; // Init by macros
GeanyData *geany_data;     // Init by macros
PLUGIN_VERSION_CHECK(147)
PLUGIN_SET_INFO("gsantner utils", "Favourites, json_reformat, vertical sidebar and various other improvements", "1.0", "Gregor Santner <gsantner@mailbox.org>")

enum SignalKeys {
	KEY_JSON_REFORMAT,
	KEYBINDING_KEYS_COUNT,
	KEY_SHOW_FAVOURITES,
};
static struct plugin_private {
	// json_reformat
	GtkWidget           *menuitem_json_reformat;   // tools menu option

	// Favourites
	GtkWidget           *menuitem_favourites;      // file menu option
	GtkMenuToolButton   *toolbar_item_favourites;  // toolbar option
	GtkWidget           *menu_favorites;           // (sub)menu containing multiple GtkMenuItem's

	// Lists
	GList                menuitem_list;            // dynamic allocated items that must be free'd
	GeanyKeyGroup       *key_group;                // Key bindings
} plugin_private;

//######################################################################################################

// Frees input string
static gchar* g_strreplace(gchar *text, const gchar *search, const gchar *replace, gboolean free_input) {
	char **split = g_strsplit(text, search, -1);
	if (free_input) {
		g_free(text);
	}
	text = g_strjoinv(replace, split);
	g_strfreev(split);
	return text;
}

static gchar *geany_config_filepath() {
	return g_build_filename(geany_data->app->configdir, "geany.conf", NULL);
}

// Get settings object for standard geany.conf file
static GKeyFile* geany_config() {
#pragma GCC diagnostic ignored "-Wunused-result"
	gchar *configfile = geany_config_filepath();
	GKeyFile *config = g_key_file_new();

	if (! g_file_test(configfile, G_FILE_TEST_IS_REGULAR)) {
		SETPTR(configfile, g_build_filename(geany_data->app->datadir, "geany.conf", NULL));
	}
	g_key_file_load_from_file(config, configfile, G_KEY_FILE_NONE, NULL);
	g_free(configfile);
	return config;
}

//######################################################################################################


// Use json_reformat executable to reformat JSON
// Write text of current open file to temporary file, json_reformat and capture output 
static void exec_json_reformat() {
	GeanyDocument	*doc;
	ScintillaObject	*sci;
	if((doc = document_get_current()) == NULL || (sci = doc->editor->sci) == NULL) {
		return;
	}

	// Current content
	gchar *text = sci_get_contents(sci, -1);
	gchar *filename = document_get_basename_for_display(doc, -1);

	// Temporary filenames
	char tmp_infile[L_tmpnam];
	close(mkstemp(tmp_infile));
	char *tmp_outfile = g_string_free(g_string_append(g_string_new(tmp_infile), ".out"), FALSE);
	// Prepare cmd
	GString *syscmd_gstring = g_string_new("/bin/cat '");
	g_string_append(syscmd_gstring, tmp_infile);
	g_string_append(syscmd_gstring, "' | json_reformat > '");
	g_string_append(syscmd_gstring, tmp_outfile);
	g_string_append(syscmd_gstring, "'");
	char *syscmd = g_string_free(syscmd_gstring, FALSE);

	// Run CMD
	g_file_set_contents(tmp_infile, text, -1, NULL);
	g_free(text); text=NULL;
	int exitc = system(syscmd);

	// Get results & delete working files
	gsize length;
	g_file_get_contents(tmp_outfile, &text, &length, NULL);
	unlink(tmp_infile);
	unlink(tmp_outfile);

	// Evaluate result
	if (exitc == 0) {
		// Set reformatted text to UI
		sci_start_undo_action(sci);
		sci_set_text(sci, text);
		sci_end_undo_action(sci);
	} else if (exitc > 256) {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] json_reformat not installed. Contained in package yajl-tools"), filename);
	} else {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] json_reformat error, code %d. The content seems not to be valid JSON."), filename, exitc);
	}

	// Free resources
	free(text);
	free(syscmd);
	free(tmp_outfile);
	free(filename);
}

//######################################################################################################

static void item_activated_open_file_in_callback_arg(GtkWidget *wid, gpointer filepath) {
	if (!g_file_test(filepath, G_FILE_TEST_IS_REGULAR) && g_str_has_prefix(filepath, "/tmp/")) {
		utils_write_file(filepath, "");
	}
    document_open_file(filepath, 0, NULL, NULL);
}

static gboolean item_activated_by_keybinding_id(guint keyid) {
	switch(keyid) {
	case KEY_JSON_REFORMAT:
		exec_json_reformat();
		break;
	case KEY_SHOW_FAVOURITES:
		gtk_menu_popup_at_pointer(GTK_MENU(gtk_menu_tool_button_get_menu(plugin_private.toolbar_item_favourites)), NULL);
		break;
	}
	return 0;
}
// Menu item activated by ID. Forward to keybinding handler
static void item_activated_by_id(GtkWidget *wid, gpointer eventdata) {
	item_activated_by_keybinding_id(GPOINTER_TO_INT(eventdata));
}

// Restyle the sidebar (containing "symbols" "files" "projects" etc)
static void restyle_sidebar() {
	GtkCssProvider *    cssProvider     = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, ""
		"*                       { background-color: #3A3D3F; } "
		".myNotebook tab         { background-color: #3A3D3F; border-right-style: solid; border-color: @theme_selected_bg_color; border-bottom-width: 1px; border-right-width: 2px; border-bottom-right-radius: 7px; border-top-right-radius: 7px; border-left-width: 0px; }"
		".myNotebook tab:checked { background-color: @theme_selected_bg_color; }"
		"*                       {  }"
		, -1, NULL);

	GtkNotebook *sidebarNotebook = GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook3"));
	GtkStyleContext * sidebarNotebookStyleContext = gtk_widget_get_style_context(GTK_WIDGET(sidebarNotebook));
	gtk_style_context_add_class(sidebarNotebookStyleContext, "myNotebook");
	gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(sidebarNotebook)), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (GList *iterator = gtk_container_get_children(GTK_CONTAINER(sidebarNotebook)); iterator; iterator = iterator->next) {
		GtkLabel *sidebarLabel = GTK_LABEL(gtk_notebook_get_tab_label(sidebarNotebook, iterator->data));
		gtk_label_set_angle(sidebarLabel, 90);

		GString *text = g_string_new(gtk_label_get_text(sidebarLabel));
		g_string_prepend(text, " <b><span foreground='#ffffff' font_desc='Monospace 10'>🔸");
		g_string_append(text, "</span></b>  ");
		gtk_label_set_markup(sidebarLabel, g_string_free(text, FALSE));
	}
}

// Hide some clutter options from menus
static void unclutter_ui() {
	gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_open_selected_file1"));
	gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_reload_as1"));
	gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_close_all1"));
	gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "close_other_documents1"));
	gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_reload_as1"));

	/* // Can be configured by settings with drag'n'drop
	GtkToolbar *toolbar = GTK_TOOLBAR(geany_data->main_widgets->toolbar);
	for (int i=0; i <  gtk_toolbar_get_n_items(toolbar); i++) {
		GtkToolItem *item = gtk_toolbar_get_nth_item(toolbar, i);
		switch (i) {
		case 2: // save
		case 3: // save all
		case 5: // reload
		case 6: // close
		case 8: // one step forward in editor (ctrl-left)
		case 9: // one step forward in editor (ctrl-right)
		case 21: // goto line button (# input in textfield + enter does the same)
			gtk_widget_hide(GTK_WIDGET(item));
			break;
		}
	}*/
}

static void add_favourites_to_menu(const gchar *dir_home, GKeyFile* config, const GtkMenu *file_menu) {
	gchar *str;
	gchar **strarr;

	// Favourites
	str = utils_get_setting_string(config, PLUGIN_NAME, "favourites", "");
	if (g_utf8_strlen(str, -1) > 0) {
		strarr = g_strsplit(str, ";", -1);

		// Setup submenu
		plugin_private.menu_favorites = gtk_menu_new();
		for (int i=0; (strarr[i] != NULL && strarr[i+1] != NULL); i+=2) {
			// First underscore is for keybinding and doesn't show up
			gchar *label = g_strreplace(g_strreplace(strarr[i], "_", "__", 0), ">>", "»", 1); 

			// Create submenu item
			GtkWidget* menuitem = NULL;
			if (g_str_equal(label, "---")) { // Separator
				menuitem = gtk_separator_menu_item_new(); i--;
			} else { // Filepath
				gchar *filepath = g_strreplace(strarr[i+1], "$HOME", dir_home, 0);
				if (g_file_test(filepath, G_FILE_TEST_IS_REGULAR) || g_str_has_prefix(filepath, "/tmp/")) {
					menuitem = ui_image_menu_item_new(NULL, label);
					g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(item_activated_open_file_in_callback_arg), filepath);
				}
			}

			// Add item to submenu
			if (menuitem != NULL) {
				gtk_menu_shell_append(GTK_MENU_SHELL(plugin_private.menu_favorites), menuitem);
				gtk_widget_show_all(menuitem);
				#pragma GCC diagnostic ignored "-Wunused-result"
				g_list_append(&plugin_private.menuitem_list, menuitem);
			}
		}

		// Show submenu - at menu
		plugin_private.menuitem_favourites = ui_image_menu_item_new("gtk-about", _("Fav"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(plugin_private.menuitem_favourites), plugin_private.menu_favorites);
		gtk_widget_show_all(plugin_private.menu_favorites);
		gtk_widget_show_all(plugin_private.menuitem_favourites);
		gtk_menu_shell_insert(GTK_MENU_SHELL(file_menu), plugin_private.menuitem_favourites, 4);

		// Show submenu - at toolbar
		plugin_private.toolbar_item_favourites = GTK_MENU_TOOL_BUTTON(gtk_menu_tool_button_new(NULL, _("Fav")));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(plugin_private.toolbar_item_favourites), "gtk-about");
		gtk_menu_tool_button_set_menu(plugin_private.toolbar_item_favourites, plugin_private.menu_favorites);
		gtk_toolbar_insert(GTK_TOOLBAR(geany_data->main_widgets->toolbar), GTK_TOOL_ITEM(plugin_private.toolbar_item_favourites), 1);
		gtk_widget_show_all(GTK_WIDGET(plugin_private.toolbar_item_favourites));
		g_signal_connect(G_OBJECT(plugin_private.toolbar_item_favourites), "clicked", G_CALLBACK(item_activated_by_id), GINT_TO_POINTER(KEY_SHOW_FAVOURITES));

		free(strarr);
	}
	free(str);
}


// Switch to message window tab by setting
// MSG_STATUS = 0, MSG_COMPILER = 1, MSG_MESSAGE = 2, MSG_SCRATCH = 3, MSG_VTE = 4
void switch_to_message_window_tab(GKeyFile *config) {
	int tab = utils_get_setting_integer(config, PLUGIN_NAME, "msgwin_tab", -1);
	if (tab >= MSG_STATUS && tab <= MSG_VTE) {
		msgwin_switch_tab(tab, FALSE);
	}
}

static void on_document_new(GObject *obj, GeanyDocument *doc, gpointer user_data) {
	ScintillaObject	*sci;
	if((sci = doc->editor->sci) == NULL) {
		return;
	}

	// Geany by default doesn't focus editor for new files, hence user needs to click before typing is possible
	// -> Focus editor and set carret to pos 0 for new documents
	gtk_widget_grab_focus(GTK_WIDGET(sci));
}



//######################################################################################################

// Init plugin
void plugin_init(GeanyData *geany_data) {
	GKeyFile *config         = geany_config();
	const GtkMenu *file_menu = GTK_MENU(ui_lookup_widget(geany_data->main_widgets->window, "file1_menu"));
	const gchar *dir_home    = g_get_home_dir();

	// Register callbacks
	plugin_signal_connect(geany_plugin, NULL, "document-new", TRUE, (GCallback) &on_document_new, NULL);

	// Hide some clutter options from menus
	unclutter_ui();

	// Switch message window tab based on settings
	switch_to_message_window_tab(config);

	// Setup Keybindings
	plugin_private.key_group = plugin_set_key_group(geany_plugin, PLUGIN_NAME, KEYBINDING_KEYS_COUNT, item_activated_by_keybinding_id);

	// JSON Reformat
	plugin_private.menuitem_json_reformat = ui_image_menu_item_new(NULL, _("json__reformat"));
	g_signal_connect(G_OBJECT(plugin_private.menuitem_json_reformat), "activate", G_CALLBACK(item_activated_by_id), GINT_TO_POINTER(KEY_JSON_REFORMAT));
	gtk_widget_show_all(plugin_private.menuitem_json_reformat);
	gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu), plugin_private.menuitem_json_reformat);
	keybindings_set_item(plugin_private.key_group, KEY_JSON_REFORMAT, NULL, 0, 0, "json_reformat", _("json_reformat"), plugin_private.menuitem_json_reformat);

	// Restyle sidebar
	restyle_sidebar();

	// Add favourites to the file menu
	add_favourites_to_menu(dir_home, config, file_menu);


/////
	// Testing code
	//gtk_label_set_angle(ui_lookup_widget(geany_data->main_widgets->window, "label137"), 90);

	//utils_get_setting_string(config, PLUGIN_NAME, "color_scheme", "aaa-solarized-greg.conf");
	//g_key_file_set_string(config, "geany", "color_scheme", "a");
	//g_key_file_save_to_file(geany_config,geany_config_filepath(), NULL);
	//filetypes_reload();
//////

	// Free resources
	free((char*) dir_home);
}


// Plugin destructor
// gtk_widget_destroy includes free()
void plugin_cleanup(void) {
	GList *iterator = NULL;

	if (GTK_IS_WIDGET(plugin_private.toolbar_item_favourites)) { 
		gtk_menu_tool_button_set_menu(plugin_private.toolbar_item_favourites, NULL);
		gtk_widget_destroy(GTK_WIDGET(plugin_private.toolbar_item_favourites)); 
	}
	if (GTK_IS_WIDGET(plugin_private.menuitem_json_reformat))  { gtk_widget_destroy(plugin_private.menuitem_json_reformat); }
	if (GTK_IS_WIDGET(plugin_private.menu_favorites))          { gtk_widget_destroy(plugin_private.menu_favorites); }
	if (GTK_IS_WIDGET(plugin_private.menuitem_favourites))     { gtk_widget_destroy(plugin_private.menuitem_favourites); }

	for (iterator = &(plugin_private.menuitem_list); iterator; iterator = iterator->next) {
		if (GTK_IS_WIDGET(iterator->data)) { gtk_widget_destroy(iterator->data); }
	}
}
