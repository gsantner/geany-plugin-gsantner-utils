//######################################################################################################
//
// > geanyplugingsantnerutils: Plugin for Geany editor (https://github.com/geany/geany)
//
// Authors:
//   2019-2023 Gregor Santner, gsantner AT mailbox DOT org
//
// License: Public domain / Creative Commons Zero 1.0
//
//######################################################################################################
//
// Dev notes:
//
// while [ true ] ; do make install && geany -v && sleep 0.2; done
// while [ true ] ; do make install && GTK_DEBUG=interactive geany -v && sleep 0.2; done
//
// msgwin_status_add ("[%s] AFTER C", label);
//
//######################################################################################################
// vim: sw=4 ts=4 ft=c noexpandtab:

// Includes
#include <geanyplugin.h>
#include <stdio.h>
#ifdef HAVE_LOCALE_H
	#include <locale.h>
#endif


// Plugin setup
const gboolean DEBUG_DOCOPEN_MSGWIN = FALSE;
const char *PLUGIN_NAME = "geanygsantnerutils";
GeanyPlugin *geany_plugin; // Init by macros
GeanyData *geany_data;     // Init by macros
PLUGIN_VERSION_CHECK(147)
PLUGIN_SET_INFO("gsantner utils", "Favourites, json_pretty, vertical sidebar and various other improvements", "1.1.0", "Gregor Santner <gsantner@mailbox.org>")

enum SignalKeys {
	GEANY_KEYS_GGU_XML_PRETTY,
	GEANY_KEYS_GGU_JSON_PRETTY,
	GEANY_KEYS_GGU_PIPE,
	GEANY_KEYS_GGU_FAVOURITES,
	GEANY_KEYS_GGU_SEARCH,
	GEANY_KEYS_GGU_COUNT,
};
static struct plugin_private {
	// Submenu options
	GtkWidget           *menuitem_json_pretty;   // tools menu option
	GtkWidget           *menuitem_xml_pretty;    // tools menu option
	GtkWidget           *menuitem_pipe;            // tools menu option

	// Favourites
	GtkWidget           *menuitem_favourites;      // file menu option
	GtkMenuToolButton   *toolbar_item_favourites;  // toolbar option
	GtkWidget           *menu_favorites;           // (sub)menu containing multiple GtkMenuItem's

	// Lists
	GList                menuitem_list;            // dynamic allocated items that must be free'd
	GeanyKeyGroup       *keybinding_group;         // Key bindings

	gboolean             current_doc_is_new;
} plugin_private;

//######################################################################################################

static void debug_doc_info_to_msgwin(GeanyDocument *doc, const char *eventname) {
	if (!DEBUG_DOCOPEN_MSGWIN || doc == NULL) {
		return;
	}
	const char *ft_ext = (doc->file_type != NULL && doc->file_type->extension != NULL) ? doc->file_type->extension : "NULL";
	msgwin_status_add ("[DEBUG/debug_doc_info_to_msgwin]: %s ft_ext >%s<, new?%d, filename >%s<)", eventname, ft_ext, plugin_private.current_doc_is_new ? 1 : 0 ,(doc->file_name != NULL ? doc->file_name : "NULL"));
}

// Frees input string
static gchar* gs_glib_strreplace(gchar *text, const gchar *search, const gchar *replace, gboolean free_input) {
	char **split = g_strsplit(text, search, -1);
	if (free_input) {
		g_free(text);
	}
	text = g_strjoinv(replace, split);
	g_strfreev(split);
	return text;
}

// Get filepath to random file/directory in temporary directory
static char* gs_glib_tmp_filepath(const char* prefix, const char* postfix) {
	gchar *uuid = g_uuid_string_random();
	GString *filename = g_string_new(prefix);
	g_string_append(filename, uuid);
	g_string_append(filename, postfix);
	gchar *fn = g_string_free(filename, FALSE);
	gchar *out = g_build_filename(g_get_tmp_dir(), fn, NULL);
	free(uuid);
	free(fn);

	return out;
}

static gchar *geany_conf_filepath() {
	return g_build_filename(geany_data->app->configdir, "geany.conf", NULL);
}

// Get settings object for standard geany.conf file
static GKeyFile* geany_conf_load() {
#pragma GCC diagnostic ignored "-Wunused-result"
	gchar *configfile = geany_conf_filepath();
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
static void exec_json_pretty() {
	GeanyDocument	*doc;
	ScintillaObject	*sci;
	if((doc = document_get_current()) == NULL || (sci = doc->editor->sci) == NULL) {
		return;
	}

	// Current content
	gchar *text = sci_get_contents(sci, -1);
	gchar *filename = document_get_basename_for_display(doc, -1);

	// Determine first [/{ and last ]/}, trim leading/trailing text (like logging prefixes)
	gchar *text_reverse = g_utf8_strreverse(text, -1);
	size_t offset_begin = MAX(0, MIN(strcspn(text, "["), strcspn(text, "{")));
	size_t offset_end   = strlen(text) - MAX(0, MIN(strcspn(text_reverse, "]"), strcspn(text_reverse, "}")));
	free(text_reverse); text_reverse = NULL;
	msgwin_status_add("JSON Pretty: offset_begin=%zd, offset_end=%zd, strlen=%zd", offset_begin, offset_end, strlen(text));

	// Prepare & Run cmd
	gchar *tmp_infile  = gs_glib_tmp_filepath(".", ".json");
	gchar *tmp_outfile = gs_glib_tmp_filepath(".", ".json");
	GString *syscmd_gstring = g_string_new("/bin/cat '");
	g_string_append(syscmd_gstring, tmp_infile);
	g_string_append(syscmd_gstring, "' | ruby -rjson -e \"puts JSON.pretty_generate(JSON.parse(ARGF.read.strip))\" > '");
	g_string_append(syscmd_gstring, tmp_outfile);
	g_string_append(syscmd_gstring, "'");
	char *syscmd = g_string_free(syscmd_gstring, FALSE);
	g_file_set_contents(tmp_infile, text+offset_begin, offset_end-offset_begin, NULL);
	int exitc = system(syscmd);
	gsize length;
	g_free(text);
	g_file_get_contents(tmp_outfile, &text, &length, NULL);
	unlink(tmp_infile);  free(tmp_infile);  tmp_infile = NULL;
	unlink(tmp_outfile); free(tmp_outfile); tmp_outfile = NULL;
	g_free(syscmd); syscmd=NULL;

	// Evaluate result
	if (exitc == 0) {
		// Set reformatted text to UI
		sci_start_undo_action(sci);
		sci_set_text(sci, text);
		sci_end_undo_action(sci);
		sci_set_current_position(sci, 0, TRUE);
	} else if (exitc > 256) {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] JSON pretty error - ruby is not installed."), filename);
	} else {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] JSON pretty error - exit code %d. The content seems not to be valid JSON."), filename, exitc);
	}

	GeanyFiletype *ft;
	if ((ft = filetypes_detect_from_file("f.json")) != NULL) {
		document_set_filetype(doc, ft);
	}

	// Free resources
	free(filename);
	free(text);
}


// Use tidy executable to reformat XML / HTML
// Write text of current open file to temporary file, tidy and capture output
static void exec_xml_pretty() {
	GeanyDocument	*doc;
	ScintillaObject	*sci;
	if((doc = document_get_current()) == NULL || (sci = doc->editor->sci) == NULL) {
		return;
	}

	// Current content
	gchar *text = sci_get_contents(sci, -1);
	gchar *filename = document_get_basename_for_display(doc, -1);

	// Determine first < and last >
	gchar *text_reverse = g_utf8_strreverse(text, -1);
	size_t offset_begin = MAX(0, strcspn(text, "<"));
	size_t offset_end   = strlen(text) - MAX(0, strcspn(text_reverse, ">"));
	free(text_reverse); text_reverse = NULL;
	msgwin_status_add("XML Pretty: offset_begin=%zd, offset_end=%zd, strlen=%zd", offset_begin, offset_end, strlen(text));

	// Prepare & Run cmd
	gchar *tmp_infile  = gs_glib_tmp_filepath(".", ".xml");
	gchar *tmp_outfile = gs_glib_tmp_filepath(".", ".xml");
	GString *syscmd_gstring = g_string_new("/bin/cat '");
	g_string_append(syscmd_gstring, tmp_infile);
	g_string_append(syscmd_gstring, "' | tidy -q -xml -w 105 --indent auto --indent-spaces 2 --indent-attributes y --quiet yes > '");
	g_string_append(syscmd_gstring, tmp_outfile);
	g_string_append(syscmd_gstring, "'");
	char *syscmd = g_string_free(syscmd_gstring, FALSE);
	g_file_set_contents(tmp_infile, text+offset_begin, offset_end-offset_begin, NULL);
	int exitc = system(syscmd);
	gsize length;
	g_free(text);
	g_file_get_contents(tmp_outfile, &text, &length, NULL);
	unlink(tmp_infile);  free(tmp_infile);  tmp_infile = NULL;
	unlink(tmp_outfile); free(tmp_outfile); tmp_outfile = NULL;
	g_free(syscmd); syscmd=NULL;

	// Evaluate result
	if (exitc == 0) {
		// Set reformatted text to UI
		sci_start_undo_action(sci);
		sci_set_text(sci, text);
		sci_end_undo_action(sci);
		sci_set_current_position(sci, 0, TRUE);
	} else if (exitc > 256) {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] tidy not installed. Contained in package tidy"), filename);
	} else {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] tidy error, code %d. The content seems not to be valid XML/HTML."), filename, exitc);
	}

	// Free resources
	free(text);
	free(filename);
}

// Pipe
static void exec_pipe() {
	GeanyDocument	*doc;
	ScintillaObject	*sci;
	if((doc = document_get_current()) == NULL || (sci = doc->editor->sci) == NULL) {
		return;
	}

	// Get pipe input
	gchar *user_input = dialogs_show_input("Pipe", GTK_WINDOW(geany->main_widgets->window), "echo current-editor-text | >>INPUT<<  > editor-text-afterwards", "grep -i ");
	if (user_input == NULL) { // canceled
		return;
	}

	// Current content
	gchar *text = sci_get_contents(sci, -1);
	gchar *filename = document_get_basename_for_display(doc, -1);


	// Prepare & Run cmd
	gchar *tmp_infile  = gs_glib_tmp_filepath(".", ".txt");
	gchar *tmp_outfile = gs_glib_tmp_filepath(".", ".txt");
	GString *syscmd_gstring = g_string_new("/bin/cat '");
	g_string_append(syscmd_gstring, tmp_infile);
	g_string_append(syscmd_gstring, "' | ");
	g_string_append(syscmd_gstring, user_input);
	g_string_append(syscmd_gstring, " > '");
	g_string_append(syscmd_gstring, tmp_outfile);
	g_string_append(syscmd_gstring, "'");
	char *syscmd = g_string_free(syscmd_gstring, FALSE);
	g_file_set_contents(tmp_infile, text, -1, NULL);
	int exitc = system(syscmd);
	gsize length;
	g_free(text);
	g_file_get_contents(tmp_outfile, &text, &length, NULL);
	unlink(tmp_infile);  free(tmp_infile);  tmp_infile = NULL;
	unlink(tmp_outfile); free(tmp_outfile); tmp_outfile = NULL;
	g_free(syscmd); syscmd=NULL;
	g_free(user_input); user_input = NULL;

	// Evaluate result
	if (exitc == 0) {
		// Set reformatted text to UI
		sci_start_undo_action(sci);
		sci_set_text(sci, text);
		sci_end_undo_action(sci);
		sci_set_current_position(sci, 0, TRUE);
	} else {
		msgwin_switch_tab(MSG_MESSAGE, 1);
		msgwin_msg_add(COLOR_RED, -1, doc, _("[%s] Error code %d. -> %s"), filename, exitc, text);
	}

	// Free resources
	free(text);
	free(filename);
}

//######################################################################################################

static void on_item_activated_open_file_in_callback_arg(GtkWidget *wid, gpointer filepath) {
	if (!g_file_test(filepath, G_FILE_TEST_IS_REGULAR) && g_str_has_prefix(filepath, "/tmp/")) {
		utils_write_file(filepath, "");
	}
    document_open_file(filepath, 0, NULL, NULL);
}

static gboolean on_item_activated_by_keybinding_id(guint keyid) {
	switch(keyid) {
	case GEANY_KEYS_GGU_JSON_PRETTY:
		exec_json_pretty();
		return TRUE;
	case GEANY_KEYS_GGU_XML_PRETTY:
		exec_xml_pretty();
		return TRUE;
	case GEANY_KEYS_GGU_PIPE:
		exec_pipe();
		return TRUE;
	case GEANY_KEYS_GGU_SEARCH:
		keybindings_send_command(GEANY_KEY_GROUP_SEARCH, GEANY_KEYS_SEARCH_FIND);
		return TRUE;
	case GEANY_KEYS_GGU_FAVOURITES:
		gtk_menu_popup_at_pointer(GTK_MENU(gtk_menu_tool_button_get_menu(plugin_private.toolbar_item_favourites)), NULL);
		return TRUE;
	}
	return FALSE;
}

// Menu item activated by ID. Forward to keybinding handler
static void on_item_activated_by_id(GtkWidget *wid, gpointer eventdata) {
	on_item_activated_by_keybinding_id(GPOINTER_TO_INT(eventdata));
}

// Restyle the sidebar (containing "symbols" "files" "projects" etc)
static void ui_debloat_and_restyle(GKeyFile* config) {
	const gboolean doSidebar = FALSE, doToolbarColor = TRUE, doToolbarHints = TRUE, doUnclutterToolbar = TRUE;

	GtkToolbar *toolbar = GTK_TOOLBAR(geany_data->main_widgets->toolbar);
	GtkNotebook *sidebarNotebook = GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook3"));
	GtkNotebook *infoNotebook = GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook_info"));
	GtkCssProvider *cssProvider; // Don't free(cssProvider)
	GtkStyleContext * cssContext = gtk_widget_get_style_context(GTK_WIDGET(sidebarNotebook));

	// Hide some clutter options from menus
	if (doUnclutterToolbar) {
		gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_open_selected_file1"));
		gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_reload_as1"));
		gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_close_all1"));
		gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "close_other_documents1"));
		gtk_widget_hide(ui_lookup_widget(geany_data->main_widgets->window, "menu_reload_as1"));
	}

	// Restyle info sidebar
	if (doSidebar) {
		gtk_css_provider_load_from_data((cssProvider = gtk_css_provider_new()), ".infoNotebook { border-left-width: 0px; }", -1, NULL);
		gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(infoNotebook)), "infoNotebook");
		gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(infoNotebook)), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);

		// Restyle sidebar
		gtk_css_provider_load_from_data((cssProvider = gtk_css_provider_new()), ""
			"*                       { background-color: #3A3D3F; } "
			".sidebarNotebook tab         { background-color: #3A3D3F; border-right-style: solid; border-top-style: solid; border-top-width: 1px; border-color: @theme_selected_bg_color; border-bottom-width: 1px; border-right-width: 2px; border-bottom-right-radius: 12px; border-top-right-radius: 12px; border-left-width: 0px; border-bottom-style: solid; margin-right: 3px; margin-bottom: 6px; }"
			".sidebarNotebook tab:checked { background-color: @theme_selected_bg_color; }"
			".sidebarNotebook             { border-left-width: 0px; border-top-width: 0px; }"
			"*                       {  }"
			, -1, NULL);
		gtk_style_context_add_class(cssContext, "sidebarNotebook");
		gtk_style_context_add_provider(cssContext, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);

		// Restyle sidebar tab labels
		gtk_css_provider_load_from_data((cssProvider = gtk_css_provider_new()), "* { color: @menu_fg_color; font-family: Monospace; font-size: 16px; font-weight: bold; }", -1, NULL);
		for (GList *iterator = gtk_container_get_children(GTK_CONTAINER(sidebarNotebook)); iterator; iterator = iterator->next) {
			GtkLabel *sidebarLabel = GTK_LABEL(gtk_notebook_get_tab_label(sidebarNotebook, iterator->data));
			gtk_label_set_angle(sidebarLabel, 90);
			cssContext = gtk_widget_get_style_context(GTK_WIDGET(sidebarLabel));
			gtk_style_context_add_provider(cssContext, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
		}
	}

	// Make toolbar same coolor like menubar - when using setting option to combine both
	if (doToolbarColor && utils_get_setting_boolean(config, "geany", "pref_toolbar_append_to_menu", FALSE)) {
		gtk_css_provider_load_from_data((cssProvider = gtk_css_provider_new()), "* { background-color: @menu_bg_color; }", -1, NULL);
		GtkStyleContext *toolbarStyleContext = gtk_widget_get_style_context(GTK_WIDGET(toolbar));
		gtk_style_context_add_provider(toolbarStyleContext, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	}

	// Toolbar: Add placeholder texts to Search/Line jump entry fields
	for (int i=0; doToolbarHints && i < gtk_toolbar_get_n_items(toolbar); i++) {
		GtkToolItem *item = gtk_toolbar_get_nth_item(toolbar, i);
		GtkWidget *child = GTK_WIDGET(gtk_bin_get_child(GTK_BIN(item)));
		const gchar *itemName = gtk_widget_get_name(GTK_WIDGET(item));
		if (g_str_equal(itemName, "SearchEntry")) {
			gtk_entry_set_placeholder_text(GTK_ENTRY(child), _("Search…"));
		} else if (g_str_equal(itemName, "GotoEntry")) {
			gtk_entry_set_placeholder_text(GTK_ENTRY(child), _("L#"));
		}
	}
}

void ui_treebrowser_plugin_load_folder(const gchar* filepath){
    if (!g_file_test(filepath, G_FILE_TEST_EXISTS|G_FILE_TEST_IS_DIR)) {
		return;
	}

	GtkComboBox *filetree_filepath_input = NULL;
	GtkNotebook *sidebarNotebook = GTK_NOTEBOOK(ui_lookup_widget(geany_data->main_widgets->window, "notebook3"));

	// Find page of treebrowser plugin (German)
	guint notebook_page;
	for (notebook_page=0; notebook_page < gtk_notebook_get_n_pages(sidebarNotebook); notebook_page++) {
		GtkWidget *child = GTK_WIDGET(gtk_notebook_get_nth_page(sidebarNotebook, notebook_page));
		if (g_str_equal(gtk_notebook_get_tab_label_text(sidebarNotebook, child), "Dateien")) {
			break;
		}
	}

	if (notebook_page < gtk_notebook_get_n_pages(sidebarNotebook)) {
		// Iterate filetree tab content children
		GtkContainer *filetree_notebook_page = GTK_CONTAINER(gtk_notebook_get_nth_page(sidebarNotebook, notebook_page));

		guint num=0;
		for (GList *iterator = gtk_container_get_children(GTK_CONTAINER(filetree_notebook_page)); iterator; iterator = iterator->next) {
			 // filetree combobox for filepath
			if (num == 2 && g_str_equal(gtk_buildable_get_name(iterator->data), "GtkComboBox")) {
				filetree_filepath_input = iterator->data;
			}
			num++;
		}
	}

	// Set custom default value
	if (filetree_filepath_input != NULL) {
		gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(filetree_filepath_input))), filepath);
		g_signal_emit_by_name(gtk_bin_get_child(GTK_BIN(filetree_filepath_input)), "activate");
	}
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
			gchar *label = gs_glib_strreplace(gs_glib_strreplace(strarr[i], "_", "__", 0), ">>", "»", 1);

			// Create submenu item
			GtkWidget* menuitem = NULL;
			if (g_str_equal(label, "---")) { // Separator
				menuitem = gtk_separator_menu_item_new(); i--;
			} else { // Filepath
				gchar *filepath = gs_glib_strreplace(strarr[i+1], "$HOME", dir_home, 0);
				if (g_file_test(filepath, G_FILE_TEST_IS_REGULAR) || g_str_has_prefix(filepath, "/tmp/")) {
					menuitem = ui_image_menu_item_new(NULL, label);
					g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(on_item_activated_open_file_in_callback_arg), filepath);
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
		g_signal_connect(G_OBJECT(plugin_private.toolbar_item_favourites), "clicked", G_CALLBACK(on_item_activated_by_id), GINT_TO_POINTER(GEANY_KEYS_GGU_FAVOURITES));

		free(strarr);
	}
	free(str);
}


// Switch to message window tab by setting
// MSG_STATUS = 0, MSG_COMPILER = 1, MSG_MESSAGE = 2, MSG_SCRATCH = 3, MSG_VTE = 4
void ui_switch_to_message_window_tab(GKeyFile *config) {
	int tab = utils_get_setting_integer(config, PLUGIN_NAME, "msgwin_tab", -1);
	if (tab >= MSG_STATUS && tab <= MSG_VTE) {
		msgwin_switch_tab(tab, FALSE);
	}
}

// Markdown filetype format doesn't have any HTML highlighting, switch to HTML for mdml by default
static void ft_use_html_syntax_for_markdown_filesuse_html_syntax_for_markdown_files(GeanyDocument *doc) {
	if (doc != NULL && doc->file_type != NULL && doc->file_type->extension != NULL && utils_str_equal(doc->file_type->extension, "mdml")) {
		GeanyFiletype *htmlType;
		if ((htmlType = filetypes_detect_from_file("f.html")) != NULL) {
			//document_set_filetype(doc, htmlType);
		}
	}
}

// Hide menu options based on filetype
static void ui_debloat_based_on_current_filetype(GeanyDocument *doc) {
	if (doc != NULL && doc->file_type != NULL && doc->file_type->extension != NULL) {
		// C / C++ File
		gboolean lang_c = (utils_str_equal(doc->file_type->extension, "c") || utils_str_equal(doc->file_type->extension, "cpp"));

		// Add #include -> Only show at C / C++ ..anyway disabled in other langs
		gtk_widget_set_visible(ui_lookup_widget(geany_data->main_widgets->window, "insert_include2"), lang_c);
	}
}

// Callback: New document opened in Geany
static void on_document_new(GObject *obj, GeanyDocument *doc, gpointer user_data) {
	debug_doc_info_to_msgwin(doc, "on_document_new");
	ScintillaObject	*sci;
	if((sci = doc->editor->sci) == NULL) {
		return;
	}

	// Geany by default doesn't focus editor for new files, hence user needs to click before typing is possible
	// -> Focus editor and set carret to pos 0 for new documents
	gtk_widget_grab_focus(GTK_WIDGET(sci));
	ft_use_html_syntax_for_markdown_filesuse_html_syntax_for_markdown_files(doc);
	ui_debloat_based_on_current_filetype(doc);

	// For new & completly empty documents, no filetype is specified
	// For use of quickly writing down something, it's useful to have Markdown highlighting
	if (FALSE && doc->file_type != NULL && doc->file_type->extension == NULL && sci_get_length(sci) < 3) {
		GeanyFiletype *ft;
		if ((ft = filetypes_detect_from_file("f.md")) != NULL) {
			document_set_filetype(doc, ft);
		}
	}
}

static void on_document_save(GObject *obj, GeanyDocument *doc, gpointer user_data) {
	debug_doc_info_to_msgwin(doc, "on_document_save");
	gboolean is_new_file = plugin_private.current_doc_is_new;
	plugin_private.current_doc_is_new = FALSE;

	const char *ft_ext_nn = (doc->file_type->extension == NULL ? "" : doc->file_type->extension);
	const gboolean is_default_ft_for_filename = (filetypes_detect_from_file(DOC_FILENAME(doc)) == doc->file_type) ? TRUE : FALSE;

	if (is_new_file) {
		// Saved a new file which had markdown filetype assigned (which may have been done by this plugin)
		if (!is_default_ft_for_filename && (g_str_equal(ft_ext_nn, "md") || g_str_equal(ft_ext_nn, "mdml"))) {
			document_set_filetype(doc, filetypes_detect_from_file(DOC_FILENAME(doc)));
		}
	}
}

// Callback: Existing document opened in Geany (not called for new file)
static void on_document_open(GObject *obj, GeanyDocument *doc, gpointer user_data) {
	debug_doc_info_to_msgwin(doc, "on_document_open");
	ft_use_html_syntax_for_markdown_filesuse_html_syntax_for_markdown_files(doc);
}

static void on_document_shown(GObject *obj, GeanyDocument *doc, gpointer user_data) {
	plugin_private.current_doc_is_new = (doc->file_name == NULL ? TRUE : FALSE);
	debug_doc_info_to_msgwin(doc, "on_document_shown");
	ui_debloat_based_on_current_filetype(doc);
}

//######################################################################################################

static gboolean plugin_post_init_200ms()  {
	ui_treebrowser_plugin_load_folder("/tmp/aatmp");
	return FALSE; // Destorys timer
}

// Init plugin
void plugin_init(GeanyData *geany_data) {
    main_locale_init(LOCALEDIR, GETTEXT_PACKAGE);
	GKeyFile *config         = geany_conf_load();
	const GtkMenu *file_menu = GTK_MENU(ui_lookup_widget(geany_data->main_widgets->window, "file1_menu"));
	const gchar *dir_home    = g_get_home_dir();

	// Register callbacks
	plugin_signal_connect(geany_plugin, NULL, "document-new",      TRUE, (GCallback) &on_document_new,   NULL);
	plugin_signal_connect(geany_plugin, NULL, "document-open",     TRUE, (GCallback) &on_document_open,  NULL);
	plugin_signal_connect(geany_plugin, NULL, "document-activate", TRUE, (GCallback) &on_document_shown, NULL);
	plugin_signal_connect(geany_plugin, NULL, "document-save",     TRUE, (GCallback) &on_document_save, NULL);

	// Switch message window tab based on settings
	ui_switch_to_message_window_tab(config);

	// Setup Keybindings
	plugin_private.keybinding_group = plugin_set_key_group(geany_plugin, PLUGIN_NAME, GEANY_KEYS_GGU_COUNT, on_item_activated_by_keybinding_id);

	// JSON Reformat
	const char *GEANY_KEYS_GGU_JSON_PRETTY_LABEL = _("[GGU] JSON pretty");
	plugin_private.menuitem_json_pretty = ui_image_menu_item_new(NULL, GEANY_KEYS_GGU_JSON_PRETTY_LABEL);
	g_signal_connect(G_OBJECT(plugin_private.menuitem_json_pretty), "activate", G_CALLBACK(on_item_activated_by_id), GINT_TO_POINTER(GEANY_KEYS_GGU_JSON_PRETTY));
	gtk_widget_show_all(plugin_private.menuitem_json_pretty);
	gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu), plugin_private.menuitem_json_pretty);
	keybindings_set_item(plugin_private.keybinding_group, GEANY_KEYS_GGU_JSON_PRETTY, NULL, 0, 0, "ggu_json_pretty", GEANY_KEYS_GGU_JSON_PRETTY_LABEL, plugin_private.menuitem_json_pretty);

	// XML Reformat
	const char *GEANY_KEYS_GGU_XML_PRETTY_LABEL = _("[GGU] XML/HTML pretty");
	plugin_private.menuitem_xml_pretty = ui_image_menu_item_new(NULL, GEANY_KEYS_GGU_XML_PRETTY_LABEL);
	g_signal_connect(G_OBJECT(plugin_private.menuitem_xml_pretty), "activate", G_CALLBACK(on_item_activated_by_id), GINT_TO_POINTER(GEANY_KEYS_GGU_XML_PRETTY));
	gtk_widget_show_all(plugin_private.menuitem_xml_pretty);
	gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu), plugin_private.menuitem_xml_pretty);
	keybindings_set_item(plugin_private.keybinding_group, GEANY_KEYS_GGU_XML_PRETTY, NULL, 0, 0, "ggu_xml_pretty", GEANY_KEYS_GGU_XML_PRETTY_LABEL, plugin_private.menuitem_xml_pretty);

	// Pipe
	const char *GEANY_KEYS_GGU_PIPE_LABEL = _("[GGU] Pipe (grep/cut/..)");
	plugin_private.menuitem_pipe = ui_image_menu_item_new(NULL, GEANY_KEYS_GGU_PIPE_LABEL);
	g_signal_connect(G_OBJECT(plugin_private.menuitem_pipe), "activate", G_CALLBACK(on_item_activated_by_id), GINT_TO_POINTER(GEANY_KEYS_GGU_PIPE));
	gtk_widget_show_all(plugin_private.menuitem_pipe);
	gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu), plugin_private.menuitem_pipe);
	keybindings_set_item(plugin_private.keybinding_group, GEANY_KEYS_GGU_PIPE, NULL, 0, 0, "ggu_pipe", GEANY_KEYS_GGU_PIPE_LABEL, plugin_private.menuitem_pipe);

	// Search (geany only allows single keybinding to one action)
	keybindings_set_item(plugin_private.keybinding_group, GEANY_KEYS_GGU_SEARCH, NULL, 0, 0, "ggu_search_dialog", _("[GGU] Seach dialog (add second search keybinding)"), NULL);

	// Restyle sidebar
	ui_debloat_and_restyle(config);

	// Add favourites to the file menu
	add_favourites_to_menu(dir_home, config, file_menu);

	// Post Init
	g_timeout_add(200, plugin_post_init_200ms, NULL);

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
	if (GTK_IS_WIDGET(plugin_private.menu_favorites))        { gtk_widget_destroy(plugin_private.menu_favorites); }
	if (GTK_IS_WIDGET(plugin_private.menuitem_favourites))   { gtk_widget_destroy(plugin_private.menuitem_favourites); }
	if (GTK_IS_WIDGET(plugin_private.menuitem_json_pretty))  { gtk_widget_destroy(plugin_private.menuitem_json_pretty); }
	if (GTK_IS_WIDGET(plugin_private.menuitem_xml_pretty))   { gtk_widget_destroy(plugin_private.menuitem_xml_pretty); }
	if (GTK_IS_WIDGET(plugin_private.menuitem_pipe))         { gtk_widget_destroy(plugin_private.menuitem_pipe); }

	for (iterator = &(plugin_private.menuitem_list); iterator; iterator = iterator->next) {
		if (GTK_IS_WIDGET(iterator->data)) { gtk_widget_destroy(iterator->data); }
	}
}
