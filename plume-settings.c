#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *code;
    const char *name;
} LayoutDef;

static const LayoutDef ALL_LAYOUTS[] = {
    { "us", "Английская (US)" },
    { "ru", "Русская (RU)" },
    { "de", "Немецкая (DE)" },
    { "fr", "Французская (FR)" },
    { "es", "Испанская (ES)" },
    { "it", "Итальянская (IT)" },
    { "pl", "Польская (PL)" },
    { "ee", "Эстонская (EE)" },
    { "fi", "Финская (FI)" },
    { "by", "Белорусская (BY)" },
    { "kz", "Казахская (KZ)" },
    { "tr", "Турецкая (TR)" },
    { "gb", "Английская (UK)" },
    { NULL, NULL }
};

static const char *const TOGGLE_IDS[] = {
    "grp:alt_shift_toggle",
    "grp:win_space_toggle",
    "grp:ctrl_shift_toggle",
    "grp:caps_toggle",
    NULL
};
static const char *const TOGGLE_LABELS[] = {
    "Alt + Shift",
    "Super + Пробел",
    "Ctrl + Shift",
    "Caps Lock",
    NULL
};

static const char *const ACCENT_PRESETS[] = {
    "#2f7fe6", "#5a8dee", "#22b573", "#e59a24", "#e04f3e", "#8b5cf6"
};
#define N_ACCENTS 6

typedef struct {
    const char *name;
    const char *mode;
    const char *accent;
} BuiltinProfile;

static const BuiltinProfile BUILTIN_PROFILES[] = {
    { "Plume Graphite", "dark",  "#2f7fe6" },
    { "Plume Light",    "light", "#2f7fe6" },
    { "Midnight",       "dark",  "#5a8dee" },
    { "Emerald",        "dark",  "#22b573" },
};
#define N_BUILTINS 4

typedef struct {
    GtkWidget *win;
    GtkWidget *stack;
    GtkCssProvider *css_provider;

    /* Внешний вид */
    char mode[16];
    char accent[16];
    char cur_wall[1024];
    GtkWidget *btn_dark;
    GtkWidget *btn_light;
    GtkWidget *swatch_btns[N_ACCENTS];
    GtkWidget *accent_hex_lbl;
    GtkWidget *wall_pic;
    GtkWidget *wall_path_lbl;
    GtkWidget *profiles_flow;
    GtkWidget *profile_entry;

    /* Клавиатура */
    GPtrArray *layouts;
    GPtrArray *addable_codes;
    GtkWidget *kbd_list;
    GtkWidget *dd_add_layout;
    GtkWidget *btn_add_layout;
    GtkWidget *dd_toggle;
    GtkWidget *kbd_status_lbl;
    gboolean loading;

    /* Сеть */
    GtkWidget *wifi_switch;
    GtkWidget *wifi_status_lbl;
    GtkWidget *wifi_list;
    GtkWidget *network_list;
    gboolean network_loading;

    /* Bluetooth */
    GtkWidget *bluetooth_switch;
    GtkWidget *bluetooth_status_lbl;
    GtkWidget *bluetooth_list;
    gboolean bluetooth_loading;

    /* Дисплеи */
    GtkWidget *display_list;

    /* Звук */
    GtkWidget *sound_output_scale;
    GtkWidget *sound_input_scale;
    GtkWidget *sound_output_enabled;
    GtkWidget *sound_input_enabled;
    GtkWidget *sound_output_label;
    GtkWidget *sound_input_label;
    gboolean sound_loading;

    /* Питание */
    GtkWidget *power_status_label;
    GtkWidget *power_profile_label;
    GtkWidget *power_profile_buttons[3];

    /* Многозадачность */
    GtkWidget *workspace_buttons[5];
    GtkWidget *workspace_status_label;

    /* Приложения */
    GtkWidget *default_app_labels[4];
    GtkWidget *autostart_status_label;
    GtkWidget *autostart_list;
} State;

static char *get_plume_root(void) {
    const char *env = getenv("PLUME_ROOT");
    if (env && *env) return g_strdup(env);
    if (g_file_test("/usr/share/plume/assets", G_FILE_TEST_IS_DIR))
        return g_strdup("/usr/share/plume");
    return g_build_filename(g_get_home_dir(), "plume", NULL);
}

static char *get_default_wallpaper(void) {
    char *root = get_plume_root();
    char *p = g_build_filename(root, "assets", "wallpaper.png", NULL);
    g_free(root);
    return p;
}

static char *get_config_dir(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "plume", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static char *get_themes_dir(void) {
    char *cfg = get_config_dir();
    char *dir = g_build_filename(cfg, "themes", NULL);
    g_free(cfg);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static const char *layout_display_name(const char *code) {
    for (int i = 0; ALL_LAYOUTS[i].code; i++) {
        if (strcmp(ALL_LAYOUTS[i].code, code) == 0)
            return ALL_LAYOUTS[i].name;
    }
    return code;
}

static int clamp_byte(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static void shift_hex_color(const char *hex, int delta, char out[16]) {
    unsigned int r = 47, g = 127, b = 230;
    if (hex && hex[0] == '#' && strlen(hex) == 7) {
        sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
    }
    g_snprintf(out, 16, "#%02x%02x%02x",
        clamp_byte((int)r + delta),
        clamp_byte((int)g + delta),
        clamp_byte((int)b + delta));
}

static void apply_window_css(State *st) {
    gboolean light = (strcmp(st->mode, "light") == 0);
    const char *bg        = light ? "#f3f5f9" : "#171a21";
    const char *bg_alt    = light ? "#e8ecf3" : "#13161c";
    const char *bg_hover  = light ? "#dce2ec" : "#1c212b";
    const char *border    = light ? "#cdd5e2" : "#252b37";
    const char *border_s  = light ? "#b8c3d4" : "#2b3342";
    const char *fg        = light ? "#171c26" : "#edf0f5";
    const char *fg_dim    = light ? "#434d5e" : "#cfd5df";
    const char *muted     = light ? "#667185" : "#8b96a8";

    char acc_hover[16];
    shift_hex_color(st->accent, light ? -16 : 18, acc_hover);

    GString *css = g_string_new("");
    g_string_append_printf(css,
        "window, window.background, window.csd {"
        "  background-color: %s; color: %s;"
        "  font-family: 'Onest', 'Sora', 'JetBrainsMono Nerd Font', 'Symbols Nerd Font', sans-serif;"
        "  font-size: 13px;"
        "}"
        "window headerbar {"
        "  background-color: %s; background-image: none; color: %s;"
        "  border-bottom: 1px solid %s; box-shadow: none;"
        "}"
        "window.plume-settings-win, window.plume-settings-win headerbar {"
        "  border-radius: 0;"
        "}"
        "window.plume-settings-win headerbar windowcontrols > button {"
        "  background: transparent; border: 1px solid transparent;"
        "  border-radius: 0; box-shadow: none;"
        "}"
        "window.plume-settings-win headerbar windowcontrols > button:hover {"
        "  background: rgba(127,127,127,0.17);"
        "}"

        "headerbar button.ps-window-btn {"
        "  min-width: 32px; min-height: 32px; padding: 0;"
        "  background: transparent; border: 0; border-radius: 0;"
        "  box-shadow: none; color: inherit;"
        "}"
        "headerbar button.ps-window-btn:hover { background: rgba(127,127,127,0.17); }"
        "headerbar button.ps-window-btn.ps-close:hover { background: #e04f3e; color: white; }"
        "box.ps-sidebar {"
        "  background-color: %s; border-right: 1px solid %s; padding: 10px 7px;"
        "}"
        ".ps-sidebar-title { font-size: 16px; font-weight: 700; padding: 4px 9px 7px 9px; }"
        "entry.ps-nav-search {"
        "  min-height: 32px; margin: 0 3px 5px 3px; padding: 2px 9px;"
        "  background: %s; color: %s; border: 1px solid %s; border-radius: 8px;"
        "}"
        "entry.ps-nav-search:focus-within { border-color: %s; }"
        "list.ps-nav { background: transparent; }"
        "list.ps-nav row { min-height: 36px; padding: 4px 10px; margin: 1px 0; border-radius: 7px; color: %s; }"
        "list.ps-nav row:hover { background-color: %s; color: %s; }"
        "list.ps-nav row:selected { background-color: %s; color: #ffffff; }"
        ".ps-nav-icon { font-size: 15px; min-width: 22px; }"
        ".ps-placeholder-icon { font-size: 34px; color: %s; }"
        "box.ps-page { padding: 20px 24px; }"
        ".ps-title { font-size: 18px; font-weight: 600; color: %s; }"
        ".ps-hero-title { font-size: 20px; font-weight: 600; color: %s; }"
        ".ps-card { background-color: %s; border: 1px solid %s; border-radius: 8px; padding: 14px 16px; }"
        ".ps-card-title { font-weight: 600; color: %s; }"
        ".ps-muted { color: %s; font-size: 12px; }"
        ".ps-tool-icon { font-size: 20px; color: %s; min-width: 28px; }"
        ".ps-preview-wrap { background-color: %s; border: 1px solid %s; border-radius: 8px; padding: 4px; }"
        "button.ps-btn {"
        "  background: %s; background-image: none; color: %s;"
        "  border: 1px solid %s; border-radius: 6px; padding: 6px 12px; box-shadow: none;"
        "}"
        "button.ps-btn:hover { background: %s; background-image: none; border-color: %s; }"
        "button.ps-btn-primary {"
        "  background: %s; background-image: none; color: #ffffff;"
        "  border: 1px solid %s; border-radius: 6px; padding: 6px 12px; box-shadow: none;"
        "}"
        "button.ps-btn-primary:hover { background: %s; background-image: none; }"
        "button.ps-btn-sm { padding: 3px 8px; min-height: 24px; min-width: 24px; }"
        "button.ps-swatch { min-width: 28px; min-height: 28px; padding: 0; border-radius: 14px; border: 2px solid transparent; background-image: none; box-shadow: none; }"
        "button.ps-swatch-active { border: 2px solid %s; }"
        "entry.ps-entry {"
        "  background: %s; color: %s; border: 1px solid %s; border-radius: 6px; padding: 5px 10px;"
        "}"
        "entry.ps-entry:focus-within { border-color: %s; }"
        "list.ps-kbd-list { background: transparent; }"
        "list.ps-kbd-list row { background: %s; border: 1px solid %s; border-radius: 6px; padding: 8px 10px; margin-bottom: 6px; }"
        "dropdown > button {"
        "  background: %s; background-image: none; color: %s;"
        "  border: 1px solid %s; border-radius: 6px; padding: 6px 12px; box-shadow: none;"
        "}"
        "dropdown > button:hover { border-color: %s; }"
        "popover > contents {"
        "  background: %s; background-image: none; border: 1px solid %s;"
        "  border-radius: 6px; color: %s; padding: 4px;"
        "}"
        "popover listview { background: transparent; color: %s; }"
        "popover listview > row { padding: 6px 10px; border-radius: 4px; color: %s; }"
        "popover listview > row:hover, popover listview > row:selected { background: %s; color: #ffffff; }",
        bg, fg,
        bg_alt, fg, border,
        bg_alt, border,
        bg, fg, border_s, st->accent,
        fg_dim, bg_hover, fg, st->accent, st->accent,
        fg, fg,
        bg_alt, border,
        fg, muted, st->accent,
        bg_alt, border,
        bg_hover, fg, border_s,
        border, st->accent,
        st->accent, st->accent, acc_hover,
        fg,
        bg, fg, border_s, st->accent,
        bg, border,
        bg_hover, fg, border_s, st->accent,
        bg, border_s, fg, fg, fg, st->accent
    );

    for (int i = 0; i < N_ACCENTS; i++) {
        g_string_append_printf(css,
            "button.ps-swatch-%d { background: %s; background-image: none; }",
            i, ACCENT_PRESETS[i]);
    }

    gtk_css_provider_load_from_string(st->css_provider, css->str);
    g_string_free(css, TRUE);

    if (st->btn_dark && st->btn_light) {
        gtk_widget_remove_css_class(st->btn_dark, "ps-btn-primary");
        gtk_widget_remove_css_class(st->btn_dark, "ps-btn");
        gtk_widget_remove_css_class(st->btn_light, "ps-btn-primary");
        gtk_widget_remove_css_class(st->btn_light, "ps-btn");
        gtk_widget_add_css_class(st->btn_dark, light ? "ps-btn" : "ps-btn-primary");
        gtk_widget_add_css_class(st->btn_light, light ? "ps-btn-primary" : "ps-btn");
    }
    for (int i = 0; i < N_ACCENTS; i++) {
        if (!st->swatch_btns[i]) continue;
        if (g_ascii_strcasecmp(st->accent, ACCENT_PRESETS[i]) == 0)
            gtk_widget_add_css_class(st->swatch_btns[i], "ps-swatch-active");
        else
            gtk_widget_remove_css_class(st->swatch_btns[i], "ps-swatch-active");
    }
    if (st->accent_hex_lbl) {
        gtk_label_set_text(GTK_LABEL(st->accent_hex_lbl), st->accent);
    }
}

static void save_and_apply_theme(State *st) {
    char *cfg = get_config_dir();
    char *ini = g_build_filename(cfg, "theme.ini", NULL);
    g_free(cfg);

    char *body = g_strdup_printf("mode=%s\naccent=%s\n", st->mode, st->accent);
    g_file_set_contents(ini, body, -1, NULL);
    g_free(body);
    g_free(ini);

    apply_window_css(st);

    char *argv[] = { (char *)"plume-apply-theme", NULL };
    GError *err = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
}

static void load_theme_prefs(State *st) {
    g_strlcpy(st->mode, "dark", sizeof(st->mode));
    g_strlcpy(st->accent, "#2f7fe6", sizeof(st->accent));

    char *cfg = get_config_dir();
    char *ini = g_build_filename(cfg, "theme.ini", NULL);
    g_free(cfg);

    char *content = NULL;
    if (g_file_get_contents(ini, &content, NULL, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "mode=")) {
                const char *v = lines[i] + 5;
                if (strcmp(v, "light") == 0 || strcmp(v, "dark") == 0)
                    g_strlcpy(st->mode, v, sizeof(st->mode));
            } else if (g_str_has_prefix(lines[i], "accent=")) {
                const char *v = lines[i] + 7;
                if (v[0] == '#' && strlen(v) == 7)
                    g_strlcpy(st->accent, v, sizeof(st->accent));
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(ini);
}

static char *read_current_wallpaper(void) {
    char *cfg = get_config_dir();
    char *wf = g_build_filename(cfg, "wallpaper", NULL);
    g_free(cfg);

    char *content = NULL;
    if (g_file_get_contents(wf, &content, NULL, NULL)) {
        g_strstrip(content);
        if (*content && g_file_test(content, G_FILE_TEST_IS_REGULAR)) {
            g_free(wf);
            return content;
        }
        g_free(content);
    }
    g_free(wf);
    return get_default_wallpaper();
}

static void apply_wallpaper(State *st, const char *path, gboolean save_custom) {
    char *cfg = get_config_dir();
    char *wf = g_build_filename(cfg, "wallpaper", NULL);
    g_free(cfg);

    if (save_custom) {
        g_file_set_contents(wf, path, -1, NULL);
    } else {
        g_unlink(wf);
    }
    g_free(wf);

    if (path && g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_strlcpy(st->cur_wall, path, sizeof(st->cur_wall));
        gtk_picture_set_filename(GTK_PICTURE(st->wall_pic), path);
        gtk_label_set_text(GTK_LABEL(st->wall_path_lbl), path);

        char *argv[] = {
            (char *)"sh", (char *)"-c",
            (char *)"pkill -x swaybg 2>/dev/null || true; exec swaybg -i \"$1\" -m fill",
            (char *)"_", (char *)path, NULL
        };
        GError *err = NULL;
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
        if (err) g_error_free(err);
    }
}

static void on_wall_picked(GObject *src, GAsyncResult *res, gpointer user_data) {
    State *st = user_data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
    if (err) { g_error_free(err); return; }
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            apply_wallpaper(st, path, TRUE);
            g_free(path);
        }
        g_object_unref(file);
    }
}

static void on_choose_wall(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Выбор обоев");

    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, "Изображения (PNG, JPG, WebP)");
    gtk_file_filter_add_mime_type(flt, "image/png");
    gtk_file_filter_add_mime_type(flt, "image/jpeg");
    gtk_file_filter_add_mime_type(flt, "image/webp");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, flt);
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));

    gtk_file_dialog_open(dlg, GTK_WINDOW(st->win), NULL, on_wall_picked, st);
    g_object_unref(flt);
    g_object_unref(filters);
    g_object_unref(dlg);
}

static void on_reset_wall(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    char *def = get_default_wallpaper();
    apply_wallpaper(st, def, FALSE);
    g_free(def);
}

static void on_mode_dark(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    g_strlcpy(st->mode, "dark", sizeof(st->mode));
    save_and_apply_theme(st);
}

static void on_mode_light(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    g_strlcpy(st->mode, "light", sizeof(st->mode));
    save_and_apply_theme(st);
}

static void on_swatch_clicked(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    const char *hex = g_object_get_data(G_OBJECT(btn), "accent-hex");
    if (!hex) return;
    g_strlcpy(st->accent, hex, sizeof(st->accent));
    save_and_apply_theme(st);
}

static void on_color_picked(GObject *src, GAsyncResult *res, gpointer user_data) {
    State *st = user_data;
    GError *err = NULL;
    GdkRGBA *rgba = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src), res, &err);
    if (err) { g_error_free(err); return; }
    if (rgba) {
        int r = clamp_byte((int)(rgba->red * 255.0 + 0.5));
        int g = clamp_byte((int)(rgba->green * 255.0 + 0.5));
        int b = clamp_byte((int)(rgba->blue * 255.0 + 0.5));
        g_snprintf(st->accent, sizeof(st->accent), "#%02x%02x%02x", r, g, b);
        gdk_rgba_free(rgba);
        save_and_apply_theme(st);
    }
}

static void on_choose_custom_color(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    GtkColorDialog *dlg = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dlg, "Выбор акцентного цвета");
    gtk_color_dialog_set_with_alpha(dlg, FALSE);
    GdkRGBA cur = { 0.18f, 0.50f, 0.90f, 1.0f };
    gdk_rgba_parse(&cur, st->accent);
    gtk_color_dialog_choose_rgba(dlg, GTK_WINDOW(st->win), &cur, NULL, on_color_picked, st);
    g_object_unref(dlg);
}

static void refresh_profiles_ui(State *st);

static void on_apply_builtin_profile(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "builtin-idx"));
    if (idx >= N_BUILTINS) return;
    g_strlcpy(st->mode, BUILTIN_PROFILES[idx].mode, sizeof(st->mode));
    g_strlcpy(st->accent, BUILTIN_PROFILES[idx].accent, sizeof(st->accent));
    save_and_apply_theme(st);
}

static void on_apply_custom_profile(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    const char *path = g_object_get_data(G_OBJECT(btn), "profile-path");
    if (!path) return;
    char *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) return;

    char wall[1024] = { 0 };
    char **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "mode=")) {
            g_strlcpy(st->mode, lines[i] + 5, sizeof(st->mode));
        } else if (g_str_has_prefix(lines[i], "accent=")) {
            g_strlcpy(st->accent, lines[i] + 7, sizeof(st->accent));
        } else if (g_str_has_prefix(lines[i], "wallpaper=")) {
            g_strlcpy(wall, lines[i] + 10, sizeof(wall));
        }
    }
    g_strfreev(lines);
    g_free(content);

    save_and_apply_theme(st);
    if (wall[0] && g_file_test(wall, G_FILE_TEST_IS_REGULAR)) {
        char *def = get_default_wallpaper();
        gboolean is_custom = (strcmp(wall, def) != 0);
        g_free(def);
        apply_wallpaper(st, wall, is_custom);
    }
}

static void on_delete_custom_profile(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    const char *path = g_object_get_data(G_OBJECT(btn), "profile-path");
    if (path) g_unlink(path);
    refresh_profiles_ui(st);
}

static void on_save_profile(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    const char *raw = gtk_editable_get_text(GTK_EDITABLE(st->profile_entry));
    char *name = g_strdup(raw ? raw : "");
    g_strstrip(name);
    if (!*name) {
        g_free(name);
        return;
    }

    char *tdir = get_themes_dir();
    char *fname = g_strdup_printf("theme-%" G_GINT64_FORMAT ".ini", g_get_real_time());
    char *fpath = g_build_filename(tdir, fname, NULL);
    g_free(fname);
    g_free(tdir);

    char *body = g_strdup_printf(
        "name=%s\nmode=%s\naccent=%s\nwallpaper=%s\n",
        name, st->mode, st->accent, st->cur_wall
    );
    g_file_set_contents(fpath, body, -1, NULL);
    g_free(body);
    g_free(fpath);
    g_free(name);

    gtk_editable_set_text(GTK_EDITABLE(st->profile_entry), "");
    refresh_profiles_ui(st);
}

static void refresh_profiles_ui(State *st) {
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(st->profiles_flow)) != NULL) {
        gtk_flow_box_remove(GTK_FLOW_BOX(st->profiles_flow), ch);
    }

    for (guint i = 0; i < N_BUILTINS; i++) {
        GtkWidget *btn = gtk_button_new_with_label(BUILTIN_PROFILES[i].name);
        gtk_widget_add_css_class(btn, "ps-btn");
        g_object_set_data(G_OBJECT(btn), "builtin-idx", GUINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_apply_builtin_profile), st);
        gtk_flow_box_append(GTK_FLOW_BOX(st->profiles_flow), btn);
    }

    char *tdir = get_themes_dir();
    GDir *dir = g_dir_open(tdir, 0, NULL);
    if (dir) {
        const char *entry;
        while ((entry = g_dir_read_name(dir)) != NULL) {
            if (!g_str_has_suffix(entry, ".ini")) continue;
            char *fpath = g_build_filename(tdir, entry, NULL);
            char *content = NULL;
            char *pname = NULL;
            if (g_file_get_contents(fpath, &content, NULL, NULL)) {
                char **lines = g_strsplit(content, "\n", -1);
                for (int i = 0; lines[i]; i++) {
                    if (g_str_has_prefix(lines[i], "name=")) {
                        pname = g_strdup(lines[i] + 5);
                        break;
                    }
                }
                g_strfreev(lines);
                g_free(content);
            }
            if (!pname || !*pname) {
                g_free(pname);
                pname = g_strdup(entry);
            }

            GtkWidget *pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget *abtn = gtk_button_new_with_label(pname);
            gtk_widget_add_css_class(abtn, "ps-btn");
            g_object_set_data_full(G_OBJECT(abtn), "profile-path", g_strdup(fpath), g_free);
            g_signal_connect(abtn, "clicked", G_CALLBACK(on_apply_custom_profile), st);

            GtkWidget *dbtn = gtk_button_new_with_label("×");
            gtk_widget_add_css_class(dbtn, "ps-btn");
            gtk_widget_add_css_class(dbtn, "ps-btn-sm");
            g_object_set_data_full(G_OBJECT(dbtn), "profile-path", g_strdup(fpath), g_free);
            g_signal_connect(dbtn, "clicked", G_CALLBACK(on_delete_custom_profile), st);

            gtk_box_append(GTK_BOX(pill), abtn);
            gtk_box_append(GTK_BOX(pill), dbtn);
            gtk_flow_box_append(GTK_FLOW_BOX(st->profiles_flow), pill);

            g_free(pname);
            g_free(fpath);
        }
        g_dir_close(dir);
    }
    g_free(tdir);
}

/* ==================== Клавиатура ==================== */

static gboolean has_layout(State *st, const char *code) {
    for (guint i = 0; i < st->layouts->len; i++) {
        if (strcmp((const char *)st->layouts->pdata[i], code) == 0)
            return TRUE;
    }
    return FALSE;
}

static void parse_env_file_for_kbd(const char *path, char **out_layout, char **out_opts) {
    char *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) return;
    char **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        if (!*out_layout && g_str_has_prefix(lines[i], "XKB_DEFAULT_LAYOUT=")) {
            char *v = g_strdup(lines[i] + strlen("XKB_DEFAULT_LAYOUT="));
            g_strstrip(v);
            *out_layout = v;
        } else if (!*out_opts && g_str_has_prefix(lines[i], "XKB_DEFAULT_OPTIONS=")) {
            char *v = g_strdup(lines[i] + strlen("XKB_DEFAULT_OPTIONS="));
            g_strstrip(v);
            *out_opts = v;
        }
    }
    g_strfreev(lines);
    g_free(content);
}

static void update_wayfire_kbd(const char *layout_csv, const char *opts) {
    const char *rt = getenv("PLUME_RUNTIME");
    if (!rt || !*rt) return;
    char *wf = g_build_filename(rt, "wayfire", "wayfire.ini", NULL);
    char *content = NULL;
    if (!g_file_get_contents(wf, &content, NULL, NULL)) {
        g_free(wf);
        return;
    }
    char **lines = g_strsplit(content, "\n", -1);
    GString *out = g_string_new("");
    for (int i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "xkb_layout")) {
            g_string_append_printf(out, "xkb_layout = %s", layout_csv);
        } else if (g_str_has_prefix(lines[i], "xkb_options")) {
            g_string_append_printf(out, "xkb_options = %s", opts);
        } else {
            g_string_append(out, lines[i]);
        }
        if (lines[i + 1]) g_string_append_c(out, '\n');
    }
    g_file_set_contents(wf, out->str, -1, NULL);
    g_string_free(out, TRUE);
    g_strfreev(lines);
    g_free(content);
    g_free(wf);
}

static void save_and_apply_keyboard(State *st) {
    GString *csv = g_string_new("");
    for (guint i = 0; i < st->layouts->len; i++) {
        if (i > 0) g_string_append_c(csv, ',');
        g_string_append(csv, (const char *)st->layouts->pdata[i]);
    }
    if (csv->len == 0) g_string_append(csv, "us");

    guint t_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->dd_toggle));
    if (t_idx > 3) t_idx = 0;
    const char *toggle = TOGGLE_IDS[t_idx];

    char *cfg = get_config_dir();
    char *kf = g_build_filename(cfg, "keyboard.env", NULL);
    g_free(cfg);

    char *body = g_strdup_printf(
        "XKB_DEFAULT_LAYOUT=%s\nXKB_DEFAULT_OPTIONS=%s\n",
        csv->str, toggle
    );
    g_file_set_contents(kf, body, -1, NULL);
    g_free(body);
    g_free(kf);

    update_wayfire_kbd(csv->str, toggle);
    g_string_free(csv, TRUE);

    gtk_label_set_text(GTK_LABEL(st->kbd_status_lbl), "Раскладки сохранены и применены в Wayfire");
}

static void refresh_keyboard_ui(State *st);

static void on_move_layout_up(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "layout-idx"));
    if (idx == 0 || idx >= st->layouts->len) return;
    gpointer tmp = st->layouts->pdata[idx - 1];
    st->layouts->pdata[idx - 1] = st->layouts->pdata[idx];
    st->layouts->pdata[idx] = tmp;
    save_and_apply_keyboard(st);
    refresh_keyboard_ui(st);
}

static void on_remove_layout(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "layout-idx"));
    if (st->layouts->len <= 1 || idx >= st->layouts->len) return;
    g_ptr_array_remove_index(st->layouts, idx);
    save_and_apply_keyboard(st);
    refresh_keyboard_ui(st);
}

static void on_add_layout(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->dd_add_layout));
    if (sel >= st->addable_codes->len) return;
    const char *code = (const char *)st->addable_codes->pdata[sel];
    if (!has_layout(st, code)) {
        g_ptr_array_add(st->layouts, g_strdup(code));
        save_and_apply_keyboard(st);
        refresh_keyboard_ui(st);
    }
}

static void on_toggle_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    State *st = user_data;
    if (st->loading) return;
    save_and_apply_keyboard(st);
}

static void refresh_keyboard_ui(State *st) {
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(st->kbd_list)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(st->kbd_list), ch);
    }

    for (guint i = 0; i < st->layouts->len; i++) {
        const char *code = (const char *)st->layouts->pdata[i];
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

        char *txt = g_strdup_printf("%u. %s", i + 1, layout_display_name(code));
        GtkWidget *lbl = gtk_label_new(txt);
        g_free(txt);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_box_append(GTK_BOX(row_box), lbl);

        if (i > 0) {
            GtkWidget *up = gtk_button_new_with_label("↑");
            gtk_widget_add_css_class(up, "ps-btn");
            gtk_widget_add_css_class(up, "ps-btn-sm");
            g_object_set_data(G_OBJECT(up), "layout-idx", GUINT_TO_POINTER(i));
            g_signal_connect(up, "clicked", G_CALLBACK(on_move_layout_up), st);
            gtk_box_append(GTK_BOX(row_box), up);
        }

        GtkWidget *del = gtk_button_new_with_label("×");
        gtk_widget_add_css_class(del, "ps-btn");
        gtk_widget_add_css_class(del, "ps-btn-sm");
        gtk_widget_set_sensitive(del, st->layouts->len > 1);
        g_object_set_data(G_OBJECT(del), "layout-idx", GUINT_TO_POINTER(i));
        g_signal_connect(del, "clicked", G_CALLBACK(on_remove_layout), st);
        gtk_box_append(GTK_BOX(row_box), del);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(st->kbd_list), row);
    }

    g_ptr_array_set_size(st->addable_codes, 0);
    GtkStringList *slist = gtk_string_list_new(NULL);
    for (int i = 0; ALL_LAYOUTS[i].code; i++) {
        if (!has_layout(st, ALL_LAYOUTS[i].code)) {
            g_ptr_array_add(st->addable_codes, g_strdup(ALL_LAYOUTS[i].code));
            gtk_string_list_append(slist, ALL_LAYOUTS[i].name);
        }
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(st->dd_add_layout), G_LIST_MODEL(slist));
    g_object_unref(slist);
    gtk_widget_set_sensitive(st->dd_add_layout, st->addable_codes->len > 0);
    gtk_widget_set_sensitive(st->btn_add_layout, st->addable_codes->len > 0);
}

static void load_keyboard_prefs(State *st) {
    char *raw_layouts = NULL;
    char *raw_opts = NULL;

    char *cfg = get_config_dir();
    char *kf = g_build_filename(cfg, "keyboard.env", NULL);
    g_free(cfg);
    parse_env_file_for_kbd(kf, &raw_layouts, &raw_opts);
    g_free(kf);
    parse_env_file_for_kbd("/etc/environment", &raw_layouts, &raw_opts);

    g_ptr_array_set_size(st->layouts, 0);
    const char *src = (raw_layouts && *raw_layouts) ? raw_layouts : "us,ru";
    char **parts = g_strsplit(src, ",", -1);
    for (int i = 0; parts[i]; i++) {
        g_strstrip(parts[i]);
        if (*parts[i] && !has_layout(st, parts[i]))
            g_ptr_array_add(st->layouts, g_strdup(parts[i]));
    }
    g_strfreev(parts);
    if (st->layouts->len == 0)
        g_ptr_array_add(st->layouts, g_strdup("us"));

    guint t_idx = 0;
    if (raw_opts) {
        for (guint j = 0; TOGGLE_IDS[j]; j++) {
            if (strcmp(raw_opts, TOGGLE_IDS[j]) == 0) { t_idx = j; break; }
        }
    }
    g_free(raw_layouts);
    g_free(raw_opts);

    st->loading = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->dd_toggle), t_idx);
    st->loading = FALSE;
    refresh_keyboard_ui(st);
}

/* ==================== Общие виджеты и страницы ==================== */

static void on_launch_tool(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    const char *cmd = g_object_get_data(G_OBJECT(btn), "tool-cmd");
    if (!cmd) return;
    char *argv[] = { (char *)cmd, NULL };
    GError *err = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
}

static void on_nav_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    if (!row) return;
    State *st = user_data;
    const char *page = g_object_get_data(G_OBJECT(row), "page-id");
    if (page) gtk_stack_set_visible_child_name(GTK_STACK(st->stack), page);
}

static GtkWidget *make_section_title(const char *text) {
    GtkWidget *lbl = gtk_label_new(text);
    gtk_widget_add_css_class(lbl, "ps-title");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    return lbl;
}

static GtkWidget *make_tool_row(const char *icon_txt, const char *title, const char *desc, const char *cmd) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *ico = gtk_label_new(icon_txt);
    gtk_widget_add_css_class(ico, "ps-tool-icon");
    gtk_box_append(GTK_BOX(card), ico);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(info, TRUE);
    GtkWidget *t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
    GtkWidget *d = gtk_label_new(desc);
    gtk_widget_add_css_class(d, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(d), 0.0f);
    gtk_box_append(GTK_BOX(info), t);
    gtk_box_append(GTK_BOX(info), d);
    gtk_box_append(GTK_BOX(card), info);

    GtkWidget *btn = gtk_button_new_with_label("Открыть");
    gtk_widget_add_css_class(btn, "ps-btn");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(btn), "tool-cmd", (gpointer)cmd);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(card), btn);

    return card;
}

static GtkWidget *make_info_row(const char *key, const char *val) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *k = gtk_label_new(key);
    gtk_widget_add_css_class(k, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(k), 0.0f);
    gtk_widget_set_size_request(k, 140, -1);

    GtkWidget *v = gtk_label_new(val);
    gtk_label_set_xalign(GTK_LABEL(v), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);

    gtk_box_append(GTK_BOX(row), k);
    gtk_box_append(GTK_BOX(row), v);
    return row;
}

static char *run_command_capture(char *const argv[]) {
    char *out = NULL;
    char *err = NULL;
    int status = 0;
    GError *error = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &out, &err, &status, &error);
    if (!ok) {
        g_clear_error(&error);
        g_free(out);
        out = g_strdup("");
    }
    g_free(err);
    return out ? out : g_strdup("");
}

static void clear_box(GtkWidget *box) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(box)) != NULL)
        gtk_box_remove(GTK_BOX(box), child);
}

static GtkWidget *make_network_row(const char *icon_text, const char *title,
    const char *details, gboolean active) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "ps-card");

    GtkWidget *icon = gtk_label_new(icon_text);
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *name = gtk_label_new(title);
    gtk_widget_add_css_class(name, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    GtkWidget *info = gtk_label_new(details);
    gtk_widget_add_css_class(info, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);
    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(text), info);
    gtk_box_append(GTK_BOX(row), text);

    if (active) {
        GtkWidget *badge = gtk_label_new("Подключено");
        gtk_widget_add_css_class(badge, "ps-card-title");
        gtk_box_append(GTK_BOX(row), badge);
    }
    return row;
}

static void refresh_wifi(State *st) {
    if (!st->wifi_list) return;
    clear_box(st->wifi_list);

    char *radio_argv[] = { "nmcli", "radio", "wifi", NULL };
    char *radio = run_command_capture(radio_argv);
    g_strstrip(radio);
    gboolean enabled = (g_strcmp0(radio, "enabled") == 0);
    st->network_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->wifi_switch), enabled);
    st->network_loading = FALSE;
    gtk_label_set_text(GTK_LABEL(st->wifi_status_lbl),
        enabled ? "Wi-Fi включён" : "Wi-Fi выключен");
    g_free(radio);

    if (!enabled) {
        GtkWidget *empty = gtk_label_new("Включи Wi-Fi, чтобы увидеть доступные сети.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->wifi_list), empty);
        return;
    }

    char *list_argv[] = {
        "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
        "device", "wifi", "list", "--rescan", "yes", NULL
    };
    char *output = run_command_capture(list_argv);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i] && count < 30; i++) {
        if (!lines[i][0]) continue;
        char **fields = g_strsplit(lines[i], ":", 4);
        if (!fields[0] || !fields[1]) {
            g_strfreev(fields);
            continue;
        }
        gboolean active = (g_strcmp0(fields[0], "*") == 0);
        const char *ssid = fields[1][0] ? fields[1] : "Скрытая сеть";
        const char *signal = fields[2] ? fields[2] : "?";
        const char *security = (fields[3] && fields[3][0]) ? fields[3] : "Открытая сеть";
        char *details = g_strdup_printf("Сигнал: %s%% · %s", signal, security);
        gtk_box_append(GTK_BOX(st->wifi_list),
            make_network_row(active ? "󰤨" : "󰤯", ssid, details, active));
        g_free(details);
        g_strfreev(fields);
        count++;
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new("Доступные сети не найдены.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->wifi_list), empty);
    }
    g_strfreev(lines);
    g_free(output);
}

static void refresh_network(State *st) {
    if (!st->network_list) return;
    clear_box(st->network_list);
    char *argv[] = {
        "nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
        "device", "status", NULL
    };
    char *output = run_command_capture(argv);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i]; i++) {
        if (!lines[i][0]) continue;
        char **fields = g_strsplit(lines[i], ":", 4);
        if (!fields[0] || !fields[1]) {
            g_strfreev(fields);
            continue;
        }
        const char *state = fields[2] ? fields[2] : "неизвестно";
        const char *connection = (fields[3] && fields[3][0]) ? fields[3] : "Нет подключения";
        gboolean active = g_str_has_prefix(state, "connected") ||
            g_str_has_prefix(state, "подключ");
        const char *icon = "󰒍";
        if (g_strcmp0(fields[1], "ethernet") == 0) icon = "󰈀";
        else if (g_strcmp0(fields[1], "wifi") == 0) icon = "󰤨";
        else if (g_strcmp0(fields[1], "bridge") == 0) icon = "󰘘";
        else if (g_strcmp0(fields[1], "wireguard") == 0 ||
                 g_strcmp0(fields[1], "tun") == 0) icon = "󰖂";
        char *details = g_strdup_printf("%s · %s · %s",
            fields[1], state, connection);
        gtk_box_append(GTK_BOX(st->network_list),
            make_network_row(icon, fields[0], details, active));
        g_free(details);
        g_strfreev(fields);
        count++;
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new("NetworkManager не вернул список устройств.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->network_list), empty);
    }
    g_strfreev(lines);
    g_free(output);
}

static gboolean on_wifi_state_set(GtkSwitch *widget, gboolean state,
    gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->network_loading) return FALSE;
    char *argv[] = { "nmcli", "radio", "wifi", state ? "on" : "off", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_wifi(st);
    return TRUE;
}

static void on_wifi_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_wifi(user_data);
}

static void on_network_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_network(user_data);
}

static void refresh_bluetooth(State *st) {
    if (!st->bluetooth_list) return;
    clear_box(st->bluetooth_list);

    char *show_argv[] = { "bluetoothctl", "show", NULL };
    char *show = run_command_capture(show_argv);
    gboolean available = strstr(show, "Controller ") != NULL;
    gboolean powered = strstr(show, "Powered: yes") != NULL;

    st->bluetooth_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->bluetooth_switch), powered);
    st->bluetooth_loading = FALSE;
    gtk_widget_set_sensitive(st->bluetooth_switch, available);
    gtk_label_set_text(GTK_LABEL(st->bluetooth_status_lbl),
        !available ? "Bluetooth-адаптер не найден" :
        powered ? "Bluetooth включён" : "Bluetooth выключен");
    g_free(show);

    if (!available || !powered) {
        GtkWidget *empty = gtk_label_new(!available
            ? "Подключи Bluetooth-адаптер, чтобы увидеть устройства."
            : "Включи Bluetooth, чтобы увидеть устройства.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->bluetooth_list), empty);
        return;
    }

    char *devices_argv[] = { "bluetoothctl", "devices", NULL };
    char *output = run_command_capture(devices_argv);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i] && count < 30; i++) {
        if (!g_str_has_prefix(lines[i], "Device ")) continue;
        char *record = g_strdup(lines[i] + 7);
        char *space = strchr(record, ' ');
        if (!space) {
            g_free(record);
            continue;
        }
        *space = '\0';
        const char *mac = record;
        const char *name = space + 1;

        char *info_argv[] = { "bluetoothctl", "info", (char *)mac, NULL };
        char *info = run_command_capture(info_argv);
        gboolean connected = strstr(info, "Connected: yes") != NULL;
        gboolean paired = strstr(info, "Paired: yes") != NULL;
        char *details = g_strdup_printf("%s%s", mac,
            paired ? " · Сопряжено" : "");
        gtk_box_append(GTK_BOX(st->bluetooth_list),
            make_network_row("󰂯", name, details, connected));
        g_free(details);
        g_free(info);
        g_free(record);
        count++;
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new("Устройства пока не найдены. Запусти поиск.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->bluetooth_list), empty);
    }
    g_strfreev(lines);
    g_free(output);
}

static gboolean on_bluetooth_state_set(GtkSwitch *widget, gboolean state,
    gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->bluetooth_loading) return FALSE;
    char *argv[] = { "bluetoothctl", "power", state ? "on" : "off", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_bluetooth(st);
    return TRUE;
}

static void on_bluetooth_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_bluetooth(user_data);
}

static gboolean on_bluetooth_scan_finished(gpointer user_data) {
    refresh_bluetooth(user_data);
    return G_SOURCE_REMOVE;
}

static void on_bluetooth_scan(GtkButton *button, gpointer user_data) {
    (void)button;
    char *argv[] = { "bluetoothctl", "--timeout", "6", "scan", "on", NULL };
    GError *error = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, NULL, &error);
    if (error) g_error_free(error);
    g_timeout_add_seconds(7, on_bluetooth_scan_finished, user_data);
}

static void refresh_displays(State *st);

static void on_display_scale(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *output = g_object_get_data(G_OBJECT(button), "output-name");
    const char *scale = g_object_get_data(G_OBJECT(button), "output-scale");
    if (!output || !scale) return;
    char *argv[] = {
        "wlr-randr", "--output", (char *)output, "--scale", (char *)scale, NULL
    };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_displays(st);
}

static GtkWidget *make_display_row(State *st, const char *output,
    const char *details) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon = gtk_label_new("󰍹");
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *name = gtk_label_new(output);
    gtk_widget_add_css_class(name, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    GtkWidget *info = gtk_label_new(details);
    gtk_widget_add_css_class(info, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);
    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(text), info);
    gtk_box_append(GTK_BOX(head), icon);
    gtk_box_append(GTK_BOX(head), text);
    gtk_box_append(GTK_BOX(card), head);

    GtkWidget *scales = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *label = gtk_label_new("Масштаб:");
    gtk_widget_add_css_class(label, "ps-muted");
    gtk_box_append(GTK_BOX(scales), label);
    const char *scale_values[] = { "1", "1.25", "1.5", "2", NULL };
    const char *scale_labels[] = { "100%", "125%", "150%", "200%", NULL };
    for (guint i = 0; scale_values[i]; i++) {
        GtkWidget *button = gtk_button_new_with_label(scale_labels[i]);
        gtk_widget_add_css_class(button, "ps-btn");
        g_object_set_data_full(G_OBJECT(button), "output-name",
            g_strdup(output), g_free);
        g_object_set_data(G_OBJECT(button), "output-scale",
            (gpointer)scale_values[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(on_display_scale), st);
        gtk_box_append(GTK_BOX(scales), button);
    }
    gtk_box_append(GTK_BOX(card), scales);
    return card;
}

static void append_display_detail(GString *details, const char *value) {
    if (details->len) g_string_append(details, " · ");
    g_string_append(details, value);
}

static void refresh_displays(State *st) {
    if (!st->display_list) return;
    clear_box(st->display_list);
    char *argv[] = { "wlr-randr", NULL };
    char *output = run_command_capture(argv);
    char **lines = g_strsplit(output, "\n", -1);
    char *current_output = NULL;
    GString *details = g_string_new(NULL);
    guint count = 0;

    for (guint i = 0; lines[i]; i++) {
        if (lines[i][0] && !g_ascii_isspace(lines[i][0])) {
            if (current_output) {
                gtk_box_append(GTK_BOX(st->display_list),
                    make_display_row(st, current_output,
                        details->len ? details->str : "Параметры не получены"));
                count++;
                g_free(current_output);
                g_string_truncate(details, 0);
            }
            char *header = g_strdup(lines[i]);
            char *space = strchr(header, ' ');
            if (space) *space = '\0';
            current_output = header;
            continue;
        }
        if (!current_output) continue;
        char *value = g_strdup(lines[i]);
        g_strstrip(value);
        if (g_str_has_prefix(value, "Enabled:") ||
            g_str_has_prefix(value, "Position:") ||
            g_str_has_prefix(value, "Scale:") ||
            g_str_has_prefix(value, "Transform:") ||
            strstr(value, "(current)")) {
            append_display_detail(details, value);
        }
        g_free(value);
    }
    if (current_output) {
        gtk_box_append(GTK_BOX(st->display_list),
            make_display_row(st, current_output,
                details->len ? details->str : "Параметры не получены"));
        count++;
        g_free(current_output);
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new(
            "Не удалось получить список дисплеев. Проверь наличие wlr-randr.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->display_list), empty);
    }
    g_string_free(details, TRUE);
    g_strfreev(lines);
    g_free(output);
}

static void on_display_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_displays(user_data);
}

static char *get_wpctl_description(const char *target) {
    char *argv[] = { "wpctl", "inspect", (char *)target, NULL };
    char *output = run_command_capture(argv);
    char *result = NULL;
    char **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        char *key = strstr(lines[i], "node.description =");
        if (!key) continue;
        char *value = strchr(key, '=');
        if (!value) continue;
        value++;
        g_strstrip(value);
        if (value[0] == '"') value++;
        size_t len = strlen(value);
        if (len && value[len - 1] == '"') value[len - 1] = '\0';
        result = g_strdup(value);
        break;
    }
    g_strfreev(lines);
    g_free(output);
    return result;
}

static gboolean read_wpctl_volume(const char *target, double *volume,
    gboolean *muted) {
    char *argv[] = { "wpctl", "get-volume", (char *)target, NULL };
    char *output = run_command_capture(argv);
    char *start = strstr(output, "Volume:");
    char *end = NULL;
    double value = 0.0;
    if (start) {
        start += strlen("Volume:");
        while (g_ascii_isspace(*start)) start++;
        value = g_ascii_strtod(start, &end);
    }
    gboolean ok = start && end != start;
    if (ok) {
        *volume = value;
        *muted = strstr(output, "[MUTED]") != NULL;
    }
    g_free(output);
    return ok;
}

static void refresh_sound(State *st) {
    double output_volume = 0.0, input_volume = 0.0;
    gboolean output_muted = FALSE, input_muted = FALSE;
    gboolean have_output = read_wpctl_volume(
        "@DEFAULT_AUDIO_SINK@", &output_volume, &output_muted);
    gboolean have_input = read_wpctl_volume(
        "@DEFAULT_AUDIO_SOURCE@", &input_volume, &input_muted);
    char *output_name = get_wpctl_description("@DEFAULT_AUDIO_SINK@");
    char *input_name = get_wpctl_description("@DEFAULT_AUDIO_SOURCE@");

    st->sound_loading = TRUE;
    gtk_widget_set_sensitive(st->sound_output_scale, have_output);
    gtk_widget_set_sensitive(st->sound_output_enabled, have_output);
    gtk_widget_set_sensitive(st->sound_input_scale, have_input);
    gtk_widget_set_sensitive(st->sound_input_enabled, have_input);
    if (have_output) {
        gtk_range_set_value(GTK_RANGE(st->sound_output_scale),
            CLAMP(output_volume * 100.0, 0.0, 150.0));
        gtk_switch_set_active(GTK_SWITCH(st->sound_output_enabled), !output_muted);
    }
    if (have_input) {
        gtk_range_set_value(GTK_RANGE(st->sound_input_scale),
            CLAMP(input_volume * 100.0, 0.0, 150.0));
        gtk_switch_set_active(GTK_SWITCH(st->sound_input_enabled), !input_muted);
    }
    st->sound_loading = FALSE;

    char *output_text = have_output
        ? g_strdup_printf("%s · %.0f%%", output_name ? output_name : "Выход",
            output_volume * 100.0)
        : g_strdup("Устройство вывода не найдено");
    char *input_text = have_input
        ? g_strdup_printf("%s · %.0f%%", input_name ? input_name : "Микрофон",
            input_volume * 100.0)
        : g_strdup("Микрофон не найден");
    gtk_label_set_text(GTK_LABEL(st->sound_output_label), output_text);
    gtk_label_set_text(GTK_LABEL(st->sound_input_label), input_text);
    g_free(output_text);
    g_free(input_text);
    g_free(output_name);
    g_free(input_name);
}

static void on_sound_volume_changed(GtkRange *range, gpointer user_data) {
    State *st = user_data;
    if (st->sound_loading) return;
    const char *target = g_object_get_data(G_OBJECT(range), "audio-target");
    if (!target) return;
    char value[24];
    g_snprintf(value, sizeof(value), "%.0f%%", gtk_range_get_value(range));
    char *argv[] = { "wpctl", "set-volume", (char *)target, value, NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);

    GtkWidget *label = g_strcmp0(target, "@DEFAULT_AUDIO_SINK@") == 0
        ? st->sound_output_label : st->sound_input_label;
    const char *old_text = gtk_label_get_text(GTK_LABEL(label));
    const char *separator = g_strrstr(old_text, " · ");
    char *new_text = separator
        ? g_strdup_printf("%.*s · %s", (int)(separator - old_text), old_text, value)
        : g_strdup_printf("%s · %s", old_text, value);
    gtk_label_set_text(GTK_LABEL(label), new_text);
    g_free(new_text);
}

static gboolean on_sound_enabled_changed(GtkSwitch *widget, gboolean state,
    gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->sound_loading) return FALSE;
    const char *target = g_object_get_data(G_OBJECT(widget), "audio-target");
    if (!target) return FALSE;
    char *argv[] = {
        "wpctl", "set-mute", (char *)target, state ? "0" : "1", NULL
    };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    return FALSE;
}

static void on_sound_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_sound(user_data);
}

static char *read_battery_status(void) {
    GDir *dir = g_dir_open("/sys/class/power_supply", 0, NULL);
    if (!dir) return g_strdup("Данные батареи недоступны");
    const char *entry;
    char *result = NULL;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        char *base = g_build_filename("/sys/class/power_supply", entry, NULL);
        char *type_path = g_build_filename(base, "type", NULL);
        char *type = NULL;
        g_file_get_contents(type_path, &type, NULL, NULL);
        g_free(type_path);
        if (!type || !g_str_has_prefix(type, "Battery")) {
            g_free(type);
            g_free(base);
            continue;
        }
        g_free(type);
        char *capacity_path = g_build_filename(base, "capacity", NULL);
        char *status_path = g_build_filename(base, "status", NULL);
        char *capacity = NULL, *status = NULL;
        g_file_get_contents(capacity_path, &capacity, NULL, NULL);
        g_file_get_contents(status_path, &status, NULL, NULL);
        if (capacity) g_strstrip(capacity);
        if (status) g_strstrip(status);
        const char *translated = "Состояние неизвестно";
        if (g_strcmp0(status, "Charging") == 0) translated = "Заряжается";
        else if (g_strcmp0(status, "Discharging") == 0) translated = "Разряжается";
        else if (g_strcmp0(status, "Full") == 0) translated = "Полностью заряжена";
        else if (g_strcmp0(status, "Not charging") == 0) translated = "Не заряжается";
        result = g_strdup_printf("%s%% · %s", capacity ? capacity : "?", translated);
        g_free(capacity);
        g_free(status);
        g_free(capacity_path);
        g_free(status_path);
        g_free(base);
        break;
    }
    g_dir_close(dir);
    return result ? result : g_strdup("Батарея не обнаружена");
}

static void refresh_power(State *st) {
    static const char *profiles[] = { "power-saver", "balanced", "performance" };
    char *get_argv[] = { "powerprofilesctl", "get", NULL };
    char *current = run_command_capture(get_argv);
    g_strstrip(current);
    char *list_argv[] = { "powerprofilesctl", "list", NULL };
    char *available = run_command_capture(list_argv);

    const char *current_label = "Служба профилей питания недоступна";
    if (g_strcmp0(current, "power-saver") == 0) current_label = "Экономия энергии";
    else if (g_strcmp0(current, "balanced") == 0) current_label = "Сбалансированный";
    else if (g_strcmp0(current, "performance") == 0) current_label = "Производительность";
    gtk_label_set_text(GTK_LABEL(st->power_profile_label), current_label);

    for (guint i = 0; i < 3; i++) {
        gtk_widget_remove_css_class(st->power_profile_buttons[i], "ps-btn-primary");
        gtk_widget_add_css_class(st->power_profile_buttons[i], "ps-btn");
        gtk_widget_set_sensitive(st->power_profile_buttons[i],
            strstr(available, profiles[i]) != NULL);
        if (g_strcmp0(current, profiles[i]) == 0) {
            gtk_widget_remove_css_class(st->power_profile_buttons[i], "ps-btn");
            gtk_widget_add_css_class(st->power_profile_buttons[i], "ps-btn-primary");
        }
    }
    char *battery = read_battery_status();
    gtk_label_set_text(GTK_LABEL(st->power_status_label), battery);
    g_free(battery);
    g_free(current);
    g_free(available);
}

static void on_power_profile(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *profile = g_object_get_data(G_OBJECT(button), "power-profile");
    if (!profile) return;
    char *argv[] = { "powerprofilesctl", "set", (char *)profile, NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_power(st);
}

static void on_power_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_power(user_data);
}

static void refresh_workspaces(State *st) {
    guint active = 0;
    for (guint i = 0; i < 5; i++) {
        char index[8];
        g_snprintf(index, sizeof(index), "%u", i + 1);
        char *argv[] = { "plume-ws", "get", index, NULL };
        char *output = run_command_capture(argv);
        gboolean is_active = strstr(output, "\"active\"") != NULL;
        gtk_widget_remove_css_class(st->workspace_buttons[i], "ps-btn");
        gtk_widget_remove_css_class(st->workspace_buttons[i], "ps-btn-primary");
        gtk_widget_add_css_class(st->workspace_buttons[i],
            is_active ? "ps-btn-primary" : "ps-btn");
        if (is_active) active = i + 1;
        g_free(output);
    }
    char *status = active
        ? g_strdup_printf("Сейчас открыт рабочий стол %u", active)
        : g_strdup("Не удалось определить активный рабочий стол");
    gtk_label_set_text(GTK_LABEL(st->workspace_status_label), status);
    g_free(status);
}

static void on_workspace_selected(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    guint index = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "workspace-index"));
    if (index < 1 || index > 5) return;
    char value[8];
    g_snprintf(value, sizeof(value), "%u", index);
    char *argv[] = { "plume-ws", "set", value, NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_workspaces(st);
}

static void on_workspace_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_workspaces(user_data);
}

static void on_settings_active_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)pspec;
    if (gtk_window_is_active(GTK_WINDOW(object)))
        refresh_workspaces(user_data);
}

static char *get_user_autostart_dir(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static gboolean load_autostart_info(const char *path, char **name,
    char **command, gboolean *disabled) {
    GKeyFile *key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(key);
        return FALSE;
    }
    *name = g_key_file_get_locale_string(key, "Desktop Entry", "Name", NULL, NULL);
    *command = g_key_file_get_string(key, "Desktop Entry", "Exec", NULL);
    GError *error = NULL;
    gboolean hidden = g_key_file_get_boolean(
        key, "Desktop Entry", "Hidden", &error);
    g_clear_error(&error);
    gboolean enabled = g_key_file_get_boolean(
        key, "Desktop Entry", "X-GNOME-Autostart-enabled", &error);
    if (error) {
        g_clear_error(&error);
        enabled = TRUE;
    }
    *disabled = hidden || !enabled;
    g_key_file_unref(key);
    return TRUE;
}

static gboolean write_autostart_entry(const char *filename, const char *name,
    const char *command, gboolean terminal) {
    char *dir = get_user_autostart_dir();
    char *path = g_build_filename(dir, filename, NULL);
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key, "Desktop Entry", "Name", name);
    g_key_file_set_string(key, "Desktop Entry", "Exec", command);
    g_key_file_set_boolean(key, "Desktop Entry", "Terminal", terminal);
    g_key_file_set_boolean(key, "Desktop Entry", "X-GNOME-Autostart-enabled", TRUE);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    gboolean ok = g_file_set_contents(path, data, length, NULL);
    g_free(data);
    g_key_file_unref(key);
    g_free(path);
    g_free(dir);
    return ok;
}

static char *safe_desktop_filename(const char *prefix, const char *value) {
    char *safe = g_strdup(value && *value ? value : "application");
    for (char *p = safe; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_' && *p != '.')
            *p = '-';
    char *result = g_strdup_printf("%s-%s.desktop", prefix, safe);
    g_free(safe);
    return result;
}

static void refresh_autostart(State *st);

static void write_system_autostart_override(const char *filename) {
    char *dir = get_user_autostart_dir();
    char *path = g_build_filename(dir, filename, NULL);
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_boolean(key, "Desktop Entry", "Hidden", TRUE);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    g_file_set_contents(path, data, length, NULL);
    g_free(data);
    g_key_file_unref(key);
    g_free(path);
    g_free(dir);
}

static void on_autostart_confirm_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        const char *filename = g_object_get_data(G_OBJECT(dialog), "filename");
        const char *action = g_object_get_data(G_OBJECT(dialog), "action");
        if (filename && g_strcmp0(action, "disable-system") == 0) {
            write_system_autostart_override(filename);
        } else if (filename && g_strcmp0(action, "delete-user") == 0) {
            char *dir = get_user_autostart_dir();
            char *path = g_build_filename(dir, filename, NULL);
            g_unlink(path);
            g_free(path);
            g_free(dir);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_autostart(st);
}

static void on_autostart_action(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *filename = g_object_get_data(G_OBJECT(button), "filename");
    const char *action = g_object_get_data(G_OBJECT(button), "action");
    if (!filename || !action) return;

    if (g_strcmp0(action, "enable-system") == 0) {
        char *dir = get_user_autostart_dir();
        char *path = g_build_filename(dir, filename, NULL);
        g_unlink(path);
        g_free(path);
        g_free(dir);
        refresh_autostart(st);
        return;
    }

    const char *message = g_strcmp0(action, "disable-system") == 0
        ? "Отключить системное приложение для этого пользователя? Системный файл не будет удалён, действие можно отменить позже."
        : "Удалить это приложение из пользовательского автозапуска?";
    const char *confirm = g_strcmp0(action, "disable-system") == 0
        ? "Отключить" : "Удалить";
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Подтверждение", GTK_WINDOW(st->win), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, confirm, GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 14);
    gtk_widget_set_margin_end(label, 14);
    gtk_box_append(GTK_BOX(content), label);
    g_object_set_data_full(G_OBJECT(dialog), "filename",
        g_strdup(filename), g_free);
    g_object_set_data_full(G_OBJECT(dialog), "action",
        g_strdup(action), g_free);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_autostart_confirm_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static GtkWidget *make_autostart_row(State *st, const char *filename,
    const char *source_path, gboolean system, gboolean disabled) {
    char *name = NULL, *command = NULL;
    gboolean file_disabled = FALSE;
    load_autostart_info(source_path, &name, &command, &file_disabled);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "ps-card");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(name ? name : filename);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *exec = gtk_label_new(command && *command
        ? command : "Команда не указана");
    gtk_widget_add_css_class(exec, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(exec), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(exec), PANGO_ELLIPSIZE_END);
    GtkWidget *source = gtk_label_new(system
        ? (disabled ? "Системное · отключено" : "Системное")
        : "Пользовательское");
    gtk_widget_add_css_class(source, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(source), 0.0f);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), exec);
    gtk_box_append(GTK_BOX(text), source);

    const char *action = system
        ? (disabled ? "enable-system" : "disable-system") : "delete-user";
    GtkWidget *button = gtk_button_new_with_label(system
        ? (disabled ? "Включить" : "Отключить") : "Удалить");
    gtk_widget_add_css_class(button, "ps-btn");
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(button), "filename",
        g_strdup(filename), g_free);
    g_object_set_data(G_OBJECT(button), "action", (gpointer)action);
    g_signal_connect(button, "clicked", G_CALLBACK(on_autostart_action), st);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(row), button);
    g_free(name);
    g_free(command);
    return row;
}

static void refresh_autostart(State *st) {
    if (!st->autostart_list) return;
    clear_box(st->autostart_list);
    char *user_dir = get_user_autostart_dir();
    guint enabled_count = 0, disabled_count = 0;

    GDir *system_dir = g_dir_open("/etc/xdg/autostart", 0, NULL);
    const char *filename;
    if (system_dir) {
        while ((filename = g_dir_read_name(system_dir)) != NULL) {
            if (!g_str_has_suffix(filename, ".desktop")) continue;
            char *system_path = g_build_filename(
                "/etc/xdg/autostart", filename, NULL);
            char *user_path = g_build_filename(user_dir, filename, NULL);
            if (g_file_test(user_path, G_FILE_TEST_IS_REGULAR)) {
                char *n = NULL, *c = NULL;
                gboolean hidden = FALSE;
                load_autostart_info(user_path, &n, &c, &hidden);
                g_free(n);
                g_free(c);
                if (!hidden) {
                    g_free(system_path);
                    g_free(user_path);
                    continue;
                }
                disabled_count++;
                gtk_box_append(GTK_BOX(st->autostart_list),
                    make_autostart_row(st, filename, system_path, TRUE, TRUE));
            } else {
                gboolean disabled = FALSE;
                char *n = NULL, *c = NULL;
                load_autostart_info(system_path, &n, &c, &disabled);
                g_free(n);
                g_free(c);
                if (disabled) disabled_count++; else enabled_count++;
                gtk_box_append(GTK_BOX(st->autostart_list),
                    make_autostart_row(st, filename, system_path, TRUE, disabled));
            }
            g_free(system_path);
            g_free(user_path);
        }
        g_dir_close(system_dir);
    }

    GDir *user_entries = g_dir_open(user_dir, 0, NULL);
    if (user_entries) {
        while ((filename = g_dir_read_name(user_entries)) != NULL) {
            if (!g_str_has_suffix(filename, ".desktop")) continue;
            char *path = g_build_filename(user_dir, filename, NULL);
            char *system_path = g_build_filename(
                "/etc/xdg/autostart", filename, NULL);
            char *n = NULL, *c = NULL;
            gboolean disabled = FALSE;
            load_autostart_info(path, &n, &c, &disabled);
            g_free(n);
            g_free(c);
            if (!disabled) {
                enabled_count++;
                gtk_box_append(GTK_BOX(st->autostart_list),
                    make_autostart_row(st, filename, path, FALSE, FALSE));
            } else if (!g_file_test(system_path, G_FILE_TEST_IS_REGULAR)) {
                disabled_count++;
            }
            g_free(system_path);
            g_free(path);
        }
        g_dir_close(user_entries);
    }

    if (!enabled_count && !disabled_count) {
        GtkWidget *empty = gtk_label_new("В автозапуске пока нет приложений.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->autostart_list), empty);
    }
    char *status = g_strdup_printf("Включено: %u · Отключено: %u",
        enabled_count, disabled_count);
    gtk_label_set_text(GTK_LABEL(st->autostart_status_label), status);
    g_free(status);
    g_free(user_dir);
}

static void refresh_applications(State *st) {
    static const char *content_types[] = {
        "text/html", "inode/directory", "image/png", "application/pdf"
    };
    for (guint i = 0; i < 4; i++) {
        GAppInfo *app = g_app_info_get_default_for_type(content_types[i], FALSE);
        gtk_label_set_text(GTK_LABEL(st->default_app_labels[i]),
            app ? g_app_info_get_display_name(app) : "Не выбрано");
        if (app) g_object_unref(app);
    }

    refresh_autostart(st);
}

static void on_app_chooser_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        const char *content_type = g_object_get_data(
            G_OBJECT(dialog), "content-type");
        GAppInfo *app = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
        if (app && content_type) {
            GError *error = NULL;
            g_app_info_set_as_default_for_type(app, content_type, &error);
            if (error) g_error_free(error);
            g_object_unref(app);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_applications(st);
}

static void on_choose_default_app(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *content_type = g_object_get_data(
        G_OBJECT(button), "content-type");
    if (!content_type) return;
    GtkWidget *dialog = gtk_app_chooser_dialog_new_for_content_type(
        GTK_WINDOW(st->win), GTK_DIALOG_MODAL, content_type);
    gtk_window_set_title(GTK_WINDOW(dialog), "Выбор приложения");
    g_object_set_data_full(G_OBJECT(dialog), "content-type",
        g_strdup(content_type), g_free);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_app_chooser_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_add_installed_app_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        GtkDropDown *dropdown = g_object_get_data(
            G_OBJECT(dialog), "app-dropdown");
        GPtrArray *apps = g_object_get_data(G_OBJECT(dialog), "app-list");
        guint selected = gtk_drop_down_get_selected(dropdown);
        if (apps && selected < apps->len) {
            GAppInfo *app = g_ptr_array_index(apps, selected);
            const char *name = g_app_info_get_display_name(app);
            const char *command = g_app_info_get_commandline(app);
            const char *id = g_app_info_get_id(app);
            if (name && command) {
                char *filename = safe_desktop_filename("plume", id ? id : name);
                write_autostart_entry(filename, name, command, FALSE);
                g_free(filename);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_autostart(st);
}

static void on_add_installed_app(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Добавить приложение", GTK_WINDOW(st->win), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, "Добавить", GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkStringList *names = gtk_string_list_new(NULL);
    GPtrArray *apps = g_ptr_array_new_with_free_func(g_object_unref);
    GList *all = g_app_info_get_all();
    for (GList *item = all; item; item = item->next) {
        GAppInfo *app = item->data;
        if (!g_app_info_should_show(app) || !g_app_info_get_commandline(app)) {
            g_object_unref(app);
            continue;
        }
        gtk_string_list_append(names, g_app_info_get_display_name(app));
        g_ptr_array_add(apps, app);
    }
    g_list_free(all);
    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(names), NULL);
    gtk_widget_set_hexpand(dropdown, TRUE);
    gtk_widget_set_margin_top(dropdown, 14);
    gtk_widget_set_margin_bottom(dropdown, 14);
    gtk_widget_set_margin_start(dropdown, 14);
    gtk_widget_set_margin_end(dropdown, 14);
    gtk_box_append(GTK_BOX(content), dropdown);
    g_object_set_data(G_OBJECT(dialog), "app-dropdown", dropdown);
    g_object_set_data_full(G_OBJECT(dialog), "app-list", apps,
        (GDestroyNotify)g_ptr_array_unref);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_add_installed_app_response), st);
    g_object_unref(names);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_add_command_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        GtkEditable *name_entry = g_object_get_data(
            G_OBJECT(dialog), "name-entry");
        GtkEditable *command_entry = g_object_get_data(
            G_OBJECT(dialog), "command-entry");
        GtkCheckButton *terminal = g_object_get_data(
            G_OBJECT(dialog), "terminal-check");
        const char *name = gtk_editable_get_text(name_entry);
        const char *command = gtk_editable_get_text(command_entry);
        if (name && *name && command && *command) {
            char stamp[32];
            g_snprintf(stamp, sizeof(stamp), "command-%" G_GINT64_FORMAT,
                g_get_real_time());
            char *filename = safe_desktop_filename("plume", stamp);
            write_autostart_entry(filename, name, command,
                gtk_check_button_get_active(terminal));
            g_free(filename);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_autostart(st);
}

static void on_add_command(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Добавить команду", GTK_WINDOW(st->win), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, "Добавить", GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(form, 14);
    gtk_widget_set_margin_bottom(form, 14);
    gtk_widget_set_margin_start(form, 14);
    gtk_widget_set_margin_end(form, 14);
    GtkWidget *name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Название");
    GtkWidget *command = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(command), "Команда запуска");
    GtkWidget *terminal = gtk_check_button_new_with_label(
        "Запускать в терминале");
    gtk_box_append(GTK_BOX(form), name);
    gtk_box_append(GTK_BOX(form), command);
    gtk_box_append(GTK_BOX(form), terminal);
    gtk_box_append(GTK_BOX(content), form);
    g_object_set_data(G_OBJECT(dialog), "name-entry", name);
    g_object_set_data(G_OBJECT(dialog), "command-entry", command);
    g_object_set_data(G_OBJECT(dialog), "terminal-check", terminal);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_add_command_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static char *read_os_pretty_name(void) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return g_strdup("Linux");
    char line[256];
    char *res = NULL;
    while (fgets(line, sizeof(line), f)) {
        if (g_str_has_prefix(line, "PRETTY_NAME=")) {
            char *v = line + strlen("PRETTY_NAME=");
            g_strstrip(v);
            size_t n = strlen(v);
            if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
                v[n - 1] = '\0';
                v++;
            }
            res = g_strdup(v);
            break;
        }
    }
    fclose(f);
    return res ? res : g_strdup("Linux");
}

static GtkWidget *build_wifi_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Wi-Fi"));

    GtkWidget *radio_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(radio_card, "ps-card");
    GtkWidget *radio_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(radio_text, TRUE);
    GtkWidget *radio_title = gtk_label_new("Беспроводная сеть");
    gtk_widget_add_css_class(radio_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(radio_title), 0.0f);
    st->wifi_status_lbl = gtk_label_new("Проверка состояния…");
    gtk_widget_add_css_class(st->wifi_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->wifi_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(radio_text), radio_title);
    gtk_box_append(GTK_BOX(radio_text), st->wifi_status_lbl);
    st->wifi_switch = gtk_switch_new();
    gtk_widget_set_valign(st->wifi_switch, GTK_ALIGN_CENTER);
    g_signal_connect(st->wifi_switch, "state-set",
        G_CALLBACK(on_wifi_state_set), st);
    gtk_box_append(GTK_BOX(radio_card), radio_text);
    gtk_box_append(GTK_BOX(radio_card), st->wifi_switch);
    gtk_box_append(GTK_BOX(box), radio_card);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить список");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_wifi_refresh), st);
    GtkWidget *editor = gtk_button_new_with_label("Дополнительные параметры");
    gtk_widget_add_css_class(editor, "ps-btn");
    g_object_set_data(G_OBJECT(editor), "tool-cmd", "nm-connection-editor");
    g_signal_connect(editor, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), editor);
    gtk_box_append(GTK_BOX(box), actions);

    st->wifi_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->wifi_list);
    refresh_wifi(st);
    return scroll;
}

static GtkWidget *build_network_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Сеть"));

    GtkWidget *description = gtk_label_new(
        "Сетевые устройства и активные подключения NetworkManager.");
    gtk_widget_add_css_class(description, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
    gtk_box_append(GTK_BOX(box), description);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_network_refresh), st);
    GtkWidget *editor = gtk_button_new_with_label("Изменить подключения");
    gtk_widget_add_css_class(editor, "ps-btn");
    g_object_set_data(G_OBJECT(editor), "tool-cmd", "nm-connection-editor");
    g_signal_connect(editor, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), editor);
    gtk_box_append(GTK_BOX(box), actions);

    st->network_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->network_list);
    refresh_network(st);
    return scroll;
}

static GtkWidget *build_bluetooth_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Bluetooth"));

    GtkWidget *power_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(power_card, "ps-card");
    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(info, TRUE);
    GtkWidget *title = gtk_label_new("Bluetooth");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    st->bluetooth_status_lbl = gtk_label_new("Проверка адаптера…");
    gtk_widget_add_css_class(st->bluetooth_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->bluetooth_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(info), title);
    gtk_box_append(GTK_BOX(info), st->bluetooth_status_lbl);
    st->bluetooth_switch = gtk_switch_new();
    gtk_widget_set_valign(st->bluetooth_switch, GTK_ALIGN_CENTER);
    g_signal_connect(st->bluetooth_switch, "state-set",
        G_CALLBACK(on_bluetooth_state_set), st);
    gtk_box_append(GTK_BOX(power_card), info);
    gtk_box_append(GTK_BOX(power_card), st->bluetooth_switch);
    gtk_box_append(GTK_BOX(box), power_card);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *scan = gtk_button_new_with_label("Искать устройства");
    gtk_widget_add_css_class(scan, "ps-btn-primary");
    g_signal_connect(scan, "clicked", G_CALLBACK(on_bluetooth_scan), st);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_bluetooth_refresh), st);
    GtkWidget *manager = gtk_button_new_with_label("Управление устройствами");
    gtk_widget_add_css_class(manager, "ps-btn");
    g_object_set_data(G_OBJECT(manager), "tool-cmd", "blueman-manager");
    g_signal_connect(manager, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), scan);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), manager);
    gtk_box_append(GTK_BOX(box), actions);

    st->bluetooth_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->bluetooth_list);
    refresh_bluetooth(st);
    return scroll;
}

static GtkWidget *build_displays_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Дисплеи"));

    GtkWidget *description = gtk_label_new(
        "Подключённые дисплеи, текущий режим и быстрый выбор масштаба.");
    gtk_widget_add_css_class(description, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
    gtk_box_append(GTK_BOX(box), description);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_display_refresh), st);
    GtkWidget *layout = gtk_button_new_with_label("Расположение дисплеев");
    gtk_widget_add_css_class(layout, "ps-btn");
    g_object_set_data(G_OBJECT(layout), "tool-cmd", "wdisplays");
    g_signal_connect(layout, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), layout);
    gtk_box_append(GTK_BOX(box), actions);

    st->display_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->display_list);
    refresh_displays(st);
    return scroll;
}

static GtkWidget *make_sound_card(State *st, const char *icon_text,
    const char *title_text, const char *target, GtkWidget **scale_out,
    GtkWidget **switch_out, GtkWidget **label_out) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon = gtk_label_new(icon_text);
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *status = gtk_label_new("Проверка устройства…");
    gtk_widget_add_css_class(status, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    GtkWidget *enabled = gtk_switch_new();
    gtk_widget_set_valign(enabled, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(enabled), "audio-target", (gpointer)target);
    g_signal_connect(enabled, "state-set",
        G_CALLBACK(on_sound_enabled_changed), st);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), status);
    gtk_box_append(GTK_BOX(head), icon);
    gtk_box_append(GTK_BOX(head), text);
    gtk_box_append(GTK_BOX(head), enabled);
    gtk_box_append(GTK_BOX(card), head);

    GtkWidget *scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0.0, 150.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_widget_set_hexpand(scale, TRUE);
    g_object_set_data(G_OBJECT(scale), "audio-target", (gpointer)target);
    g_signal_connect(scale, "value-changed",
        G_CALLBACK(on_sound_volume_changed), st);
    gtk_box_append(GTK_BOX(card), scale);
    *scale_out = scale;
    *switch_out = enabled;
    *label_out = status;
    return card;
}

static GtkWidget *build_sound_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Звук"));

    gtk_box_append(GTK_BOX(box), make_sound_card(st, "󰕾", "Вывод звука",
        "@DEFAULT_AUDIO_SINK@", &st->sound_output_scale,
        &st->sound_output_enabled, &st->sound_output_label));
    gtk_box_append(GTK_BOX(box), make_sound_card(st, "󰍬", "Микрофон",
        "@DEFAULT_AUDIO_SOURCE@", &st->sound_input_scale,
        &st->sound_input_enabled, &st->sound_input_label));

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_sound_refresh), st);
    GtkWidget *mixer = gtk_button_new_with_label("Расширенный микшер");
    gtk_widget_add_css_class(mixer, "ps-btn");
    g_object_set_data(G_OBJECT(mixer), "tool-cmd", "pavucontrol");
    g_signal_connect(mixer, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), mixer);
    gtk_box_append(GTK_BOX(box), actions);
    refresh_sound(st);
    return scroll;
}

static GtkWidget *build_power_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Питание"));

    GtkWidget *battery_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(battery_card, "ps-card");
    GtkWidget *battery_icon = gtk_label_new("󰁹");
    gtk_widget_add_css_class(battery_icon, "ps-tool-icon");
    GtkWidget *battery_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *battery_title = gtk_label_new("Батарея");
    gtk_widget_add_css_class(battery_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(battery_title), 0.0f);
    st->power_status_label = gtk_label_new("Проверка состояния…");
    gtk_widget_add_css_class(st->power_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->power_status_label), 0.0f);
    gtk_box_append(GTK_BOX(battery_text), battery_title);
    gtk_box_append(GTK_BOX(battery_text), st->power_status_label);
    gtk_box_append(GTK_BOX(battery_card), battery_icon);
    gtk_box_append(GTK_BOX(battery_card), battery_text);
    gtk_box_append(GTK_BOX(box), battery_card);

    GtkWidget *profile_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(profile_card, "ps-card");
    GtkWidget *profile_title = gtk_label_new("Профиль энергопотребления");
    gtk_widget_add_css_class(profile_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(profile_title), 0.0f);
    st->power_profile_label = gtk_label_new("Проверка профилей…");
    gtk_widget_add_css_class(st->power_profile_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->power_profile_label), 0.0f);
    gtk_box_append(GTK_BOX(profile_card), profile_title);
    gtk_box_append(GTK_BOX(profile_card), st->power_profile_label);
    GtkWidget *profiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *profile_ids[] = { "power-saver", "balanced", "performance" };
    const char *profile_labels[] = {
        "Экономия энергии", "Сбалансированный", "Производительность"
    };
    for (guint i = 0; i < 3; i++) {
        GtkWidget *button = gtk_button_new_with_label(profile_labels[i]);
        g_object_set_data(G_OBJECT(button), "power-profile",
            (gpointer)profile_ids[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(on_power_profile), st);
        st->power_profile_buttons[i] = button;
        gtk_box_append(GTK_BOX(profiles), button);
    }
    gtk_box_append(GTK_BOX(profile_card), profiles);
    gtk_box_append(GTK_BOX(box), profile_card);

    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    gtk_widget_set_halign(refresh, GTK_ALIGN_START);
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_power_refresh), st);
    gtk_box_append(GTK_BOX(box), refresh);
    refresh_power(st);
    return scroll;
}

static GtkWidget *build_multitasking_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Многозадачность"));

    GtkWidget *workspace_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(workspace_card, "ps-card");
    GtkWidget *title = gtk_label_new("Рабочие столы");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    st->workspace_status_label = gtk_label_new("Проверка рабочего стола…");
    gtk_widget_add_css_class(st->workspace_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->workspace_status_label), 0.0f);
    gtk_box_append(GTK_BOX(workspace_card), title);
    gtk_box_append(GTK_BOX(workspace_card), st->workspace_status_label);
    GtkWidget *workspaces = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (guint i = 0; i < 5; i++) {
        char label[8];
        g_snprintf(label, sizeof(label), "%u", i + 1);
        GtkWidget *button = gtk_button_new_with_label(label);
        g_object_set_data(G_OBJECT(button), "workspace-index",
            GUINT_TO_POINTER(i + 1));
        g_signal_connect(button, "clicked",
            G_CALLBACK(on_workspace_selected), st);
        st->workspace_buttons[i] = button;
        gtk_box_append(GTK_BOX(workspaces), button);
    }
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_workspace_refresh), st);
    gtk_box_append(GTK_BOX(workspaces), refresh);
    gtk_box_append(GTK_BOX(workspace_card), workspaces);
    gtk_box_append(GTK_BOX(box), workspace_card);

    GtkWidget *shortcuts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(shortcuts, "ps-card");
    GtkWidget *shortcuts_title = gtk_label_new("Управление окнами");
    gtk_widget_add_css_class(shortcuts_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(shortcuts_title), 0.0f);
    gtk_box_append(GTK_BOX(shortcuts), shortcuts_title);
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Рабочие столы", "Ctrl + Super + ← / →"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Привязка окна", "Super + ← / →"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Развернуть", "Super + ↑"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Восстановить", "Super + ↓"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Переключение окон", "Alt + Tab"));
    gtk_box_append(GTK_BOX(box), shortcuts);
    refresh_workspaces(st);
    return scroll;
}

static GtkWidget *make_default_app_row(State *st, guint index,
    const char *title_text, const char *content_type) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *current = gtk_label_new("Проверка…");
    gtk_widget_add_css_class(current, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(current), 0.0f);
    st->default_app_labels[index] = current;
    GtkWidget *choose = gtk_button_new_with_label("Изменить");
    gtk_widget_add_css_class(choose, "ps-btn");
    g_object_set_data(G_OBJECT(choose), "content-type",
        (gpointer)content_type);
    g_signal_connect(choose, "clicked",
        G_CALLBACK(on_choose_default_app), st);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), current);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(row), choose);
    return row;
}

static GtkWidget *build_applications_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Приложения"));

    GtkWidget *defaults = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(defaults, "ps-card");
    GtkWidget *defaults_title = gtk_label_new("Приложения по умолчанию");
    gtk_widget_add_css_class(defaults_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(defaults_title), 0.0f);
    gtk_box_append(GTK_BOX(defaults), defaults_title);
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 0, "Веб-браузер", "text/html"));
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 1, "Файловый менеджер", "inode/directory"));
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 2, "Просмотр изображений", "image/png"));
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 3, "Документы PDF", "application/pdf"));
    gtk_box_append(GTK_BOX(box), defaults);

    GtkWidget *autostart = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(autostart, "ps-card");
    GtkWidget *autostart_title = gtk_label_new("Автозапуск");
    gtk_widget_add_css_class(autostart_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(autostart_title), 0.0f);
    st->autostart_status_label = gtk_label_new("Проверка…");
    gtk_widget_add_css_class(st->autostart_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->autostart_status_label), 0.0f);
    GtkWidget *autostart_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *add_app = gtk_button_new_with_label("Добавить приложение");
    gtk_widget_add_css_class(add_app, "ps-btn-primary");
    g_signal_connect(add_app, "clicked", G_CALLBACK(on_add_installed_app), st);
    GtkWidget *add_command = gtk_button_new_with_label("Добавить команду");
    gtk_widget_add_css_class(add_command, "ps-btn");
    g_signal_connect(add_command, "clicked", G_CALLBACK(on_add_command), st);
    gtk_box_append(GTK_BOX(autostart_actions), add_app);
    gtk_box_append(GTK_BOX(autostart_actions), add_command);
    gtk_box_append(GTK_BOX(autostart), autostart_title);
    gtk_box_append(GTK_BOX(autostart), st->autostart_status_label);
    gtk_box_append(GTK_BOX(autostart), autostart_actions);
    gtk_box_append(GTK_BOX(box), autostart);

    st->autostart_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->autostart_list);
    refresh_applications(st);
    return scroll;
}

static GtkWidget *build_appearance_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);

    gtk_box_append(GTK_BOX(box), make_section_title("Внешний вид"));

    /* Карточка: Стиль и акцент */
    GtkWidget *style_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(style_card, "ps-card");

    GtkWidget *m_lbl = gtk_label_new("Тема интерфейса");
    gtk_widget_add_css_class(m_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(m_lbl), 0.0f);
    gtk_box_append(GTK_BOX(style_card), m_lbl);

    GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->btn_dark = gtk_button_new_with_label("Тёмная");
    st->btn_light = gtk_button_new_with_label("Светлая");
    g_signal_connect(st->btn_dark, "clicked", G_CALLBACK(on_mode_dark), st);
    g_signal_connect(st->btn_light, "clicked", G_CALLBACK(on_mode_light), st);
    gtk_box_append(GTK_BOX(mode_row), st->btn_dark);
    gtk_box_append(GTK_BOX(mode_row), st->btn_light);
    gtk_box_append(GTK_BOX(style_card), mode_row);

    GtkWidget *a_lbl = gtk_label_new("Акцентный цвет");
    gtk_widget_add_css_class(a_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(a_lbl), 0.0f);
    gtk_box_append(GTK_BOX(style_card), a_lbl);

    GtkWidget *acc_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (int i = 0; i < N_ACCENTS; i++) {
        GtkWidget *sw = gtk_button_new();
        gtk_widget_add_css_class(sw, "ps-swatch");
        char cls[32];
        g_snprintf(cls, sizeof(cls), "ps-swatch-%d", i);
        gtk_widget_add_css_class(sw, cls);
        g_object_set_data(G_OBJECT(sw), "accent-hex", (gpointer)ACCENT_PRESETS[i]);
        g_signal_connect(sw, "clicked", G_CALLBACK(on_swatch_clicked), st);
        st->swatch_btns[i] = sw;
        gtk_box_append(GTK_BOX(acc_row), sw);
    }

    GtkWidget *custom_c = gtk_button_new_with_label("Свой цвет…");
    gtk_widget_add_css_class(custom_c, "ps-btn");
    g_signal_connect(custom_c, "clicked", G_CALLBACK(on_choose_custom_color), st);
    gtk_box_append(GTK_BOX(acc_row), custom_c);

    st->accent_hex_lbl = gtk_label_new(st->accent);
    gtk_widget_add_css_class(st->accent_hex_lbl, "ps-muted");
    gtk_box_append(GTK_BOX(acc_row), st->accent_hex_lbl);
    gtk_box_append(GTK_BOX(style_card), acc_row);

    gtk_box_append(GTK_BOX(box), style_card);

    /* Карточка: Персональные темы */
    GtkWidget *prof_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(prof_card, "ps-card");

    GtkWidget *p_lbl = gtk_label_new("Персональные темы");
    gtk_widget_add_css_class(p_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(p_lbl), 0.0f);
    gtk_box_append(GTK_BOX(prof_card), p_lbl);

    st->profiles_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(st->profiles_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(st->profiles_flow), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(st->profiles_flow), 6);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(st->profiles_flow), 6);
    gtk_box_append(GTK_BOX(prof_card), st->profiles_flow);

    GtkWidget *save_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->profile_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->profile_entry, "ps-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->profile_entry), "Название своей темы…");
    gtk_widget_set_hexpand(st->profile_entry, TRUE);
    GtkWidget *save_btn = gtk_button_new_with_label("Сохранить текущую");
    gtk_widget_add_css_class(save_btn, "ps-btn");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_profile), st);
    gtk_box_append(GTK_BOX(save_row), st->profile_entry);
    gtk_box_append(GTK_BOX(save_row), save_btn);
    gtk_box_append(GTK_BOX(prof_card), save_row);
    gtk_box_append(GTK_BOX(box), prof_card);

    refresh_profiles_ui(st);

    /* Карточка: Обои */
    GtkWidget *wall_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(wall_card, "ps-card");

    GtkWidget *w_lbl = gtk_label_new("Обои рабочего стола");
    gtk_widget_add_css_class(w_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(w_lbl), 0.0f);
    gtk_box_append(GTK_BOX(wall_card), w_lbl);

    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "ps-preview-wrap");
    st->wall_pic = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(st->wall_pic), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_size_request(st->wall_pic, 440, 185);
    gtk_box_append(GTK_BOX(frame), st->wall_pic);
    gtk_box_append(GTK_BOX(wall_card), frame);

    st->wall_path_lbl = gtk_label_new("");
    gtk_widget_add_css_class(st->wall_path_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->wall_path_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(st->wall_path_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(wall_card), st->wall_path_lbl);

    char *cur = read_current_wallpaper();
    if (g_file_test(cur, G_FILE_TEST_IS_REGULAR)) {
        g_strlcpy(st->cur_wall, cur, sizeof(st->cur_wall));
        gtk_picture_set_filename(GTK_PICTURE(st->wall_pic), cur);
        gtk_label_set_text(GTK_LABEL(st->wall_path_lbl), cur);
    }
    g_free(cur);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *pick = gtk_button_new_with_label("Выбрать файл…");
    gtk_widget_add_css_class(pick, "ps-btn-primary");
    g_signal_connect(pick, "clicked", G_CALLBACK(on_choose_wall), st);

    GtkWidget *reset = gtk_button_new_with_label("Сбросить на стандартные");
    gtk_widget_add_css_class(reset, "ps-btn");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_wall), st);

    gtk_box_append(GTK_BOX(btns), pick);
    gtk_box_append(GTK_BOX(btns), reset);
    gtk_box_append(GTK_BOX(wall_card), btns);

    gtk_box_append(GTK_BOX(box), wall_card);
    return scroll;
}

static GtkWidget *build_keyboard_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Клавиатура и раскладки"));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *l1 = gtk_label_new("Активные раскладки");
    gtk_widget_add_css_class(l1, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
    gtk_box_append(GTK_BOX(card), l1);

    st->kbd_list = gtk_list_box_new();
    gtk_widget_add_css_class(st->kbd_list, "ps-kbd-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->kbd_list), GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(card), st->kbd_list);

    GtkWidget *add_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->dd_add_layout = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_hexpand(st->dd_add_layout, TRUE);
    st->btn_add_layout = gtk_button_new_with_label("+ Добавить раскладку");
    gtk_widget_add_css_class(st->btn_add_layout, "ps-btn-primary");
    g_signal_connect(st->btn_add_layout, "clicked", G_CALLBACK(on_add_layout), st);
    gtk_box_append(GTK_BOX(add_row), st->dd_add_layout);
    gtk_box_append(GTK_BOX(add_row), st->btn_add_layout);
    gtk_box_append(GTK_BOX(card), add_row);

    gtk_box_append(GTK_BOX(box), card);

    GtkWidget *tcard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(tcard, "ps-card");
    GtkWidget *l2 = gtk_label_new("Комбинация переключения");
    gtk_widget_add_css_class(l2, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
    st->dd_toggle = gtk_drop_down_new_from_strings(TOGGLE_LABELS);
    gtk_box_append(GTK_BOX(tcard), l2);
    gtk_box_append(GTK_BOX(tcard), st->dd_toggle);
    gtk_box_append(GTK_BOX(box), tcard);

    st->kbd_status_lbl = gtk_label_new("Подхватывает системные раскладки; изменения сохраняются в ~/.config/plume/keyboard.env");
    gtk_widget_add_css_class(st->kbd_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->kbd_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), st->kbd_status_lbl);

    load_keyboard_prefs(st);
    g_signal_connect(st->dd_toggle, "notify::selected", G_CALLBACK(on_toggle_changed), st);

    return box;
}

static GtkWidget *build_system_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Система и устройства"));

    gtk_box_append(GTK_BOX(box), make_tool_row(
        "󰕾", "Звук", "Громкость, устройства вывода и микрофон (pavucontrol)", "pavucontrol"
    ));
    gtk_box_append(GTK_BOX(box), make_tool_row(
        "󰈀", "Сеть и Wi-Fi", "Проводные и беспроводные соединения (NetworkManager)", "nm-connection-editor"
    ));
    gtk_box_append(GTK_BOX(box), make_tool_row(
        "󰂯", "Bluetooth", "Поиск, сопряжение и устройства Bluetooth (Blueman)", "blueman-manager"
    ));
    return box;
}

static GtkWidget *build_about_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(hero, "ps-card");

    char *root = get_plume_root();
    char *logo = g_build_filename(root, "assets", "plume-logo-clean.svg", NULL);
    g_free(root);
    GtkWidget *img = g_file_test(logo, G_FILE_TEST_IS_REGULAR)
        ? gtk_image_new_from_file(logo)
        : gtk_image_new_from_icon_name("preferences-system");
    gtk_image_set_pixel_size(GTK_IMAGE(img), 56);
    g_free(logo);
    gtk_box_append(GTK_BOX(hero), img);

    GtkWidget *htxt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(htxt, GTK_ALIGN_CENTER);
    GtkWidget *name = gtk_label_new("Plume");
    gtk_widget_add_css_class(name, "ps-hero-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    GtkWidget *ver = gtk_label_new("Версия 0.1.0-beta1 · Wayland Desktop Environment");
    gtk_widget_add_css_class(ver, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(ver), 0.0f);
    gtk_box_append(GTK_BOX(htxt), name);
    gtk_box_append(GTK_BOX(htxt), ver);
    gtk_box_append(GTK_BOX(hero), htxt);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    char *os_name = read_os_pretty_name();
    struct utsname u;
    const char *kernel = (uname(&u) == 0) ? u.release : "неизвестно";

    gtk_box_append(GTK_BOX(card), make_info_row("Система", os_name));
    gtk_box_append(GTK_BOX(card), make_info_row("Ядро", kernel));
    gtk_box_append(GTK_BOX(card), make_info_row("Композитор", "Wayfire (wlroots · Wayland)"));
    gtk_box_append(GTK_BOX(card), make_info_row("Компоненты", "Waybar · Mako · GTK4"));
    gtk_box_append(GTK_BOX(card), make_info_row("Репозиторий", "github.com/etern4llyfunny/plume"));
    g_free(os_name);

    gtk_box_append(GTK_BOX(box), card);
    return box;
}

static GtkWidget *build_tool_page(const char *title, const char *icon_txt,
    const char *tool_title, const char *description, const char *cmd) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title(title));
    gtk_box_append(GTK_BOX(box), make_tool_row(icon_txt, tool_title, description, cmd));
    return box;
}

static GtkWidget *build_placeholder_page(const char *title, const char *icon_txt,
    const char *description) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title(title));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");
    GtkWidget *icon = gtk_label_new(icon_txt);
    gtk_widget_add_css_class(icon, "ps-placeholder-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_START);
    GtkWidget *status = gtk_label_new("Раздел готовится");
    gtk_widget_add_css_class(status, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    GtkWidget *text = gtk_label_new(description);
    gtk_widget_add_css_class(text, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(text), TRUE);
    gtk_box_append(GTK_BOX(card), icon);
    gtk_box_append(GTK_BOX(card), status);
    gtk_box_append(GTK_BOX(card), text);
    gtk_box_append(GTK_BOX(box), card);
    return box;
}

static void on_nav_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    GtkWidget *nav = GTK_WIDGET(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    char *needle = g_utf8_casefold(text ? text : "", -1);

    for (GtkWidget *row = gtk_widget_get_first_child(nav); row;
         row = gtk_widget_get_next_sibling(row)) {
        const char *label = g_object_get_data(G_OBJECT(row), "search-label");
        gboolean visible = !needle[0] || (label && g_strstr_len(label, -1, needle));
        gtk_widget_set_visible(row, visible);
    }
    g_free(needle);
}

static void add_nav_item(GtkWidget *list, const char *icon_txt, const char *label_txt, const char *page_id) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *ico = gtk_label_new(icon_txt);
    gtk_widget_add_css_class(ico, "ps-nav-icon");
    GtkWidget *lbl = gtk_label_new(label_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(hbox), ico);
    gtk_box_append(GTK_BOX(hbox), lbl);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    g_object_set_data(G_OBJECT(row), "page-id", (gpointer)page_id);
    g_object_set_data_full(G_OBJECT(row), "search-label",
        g_utf8_casefold(label_txt, -1), g_free);
    gtk_list_box_append(GTK_LIST_BOX(list), row);
}


static void on_activate(GtkApplication *app, gpointer user_data) {
    State *st = user_data;
    if (st->win) {
        gtk_window_present(GTK_WINDOW(st->win));
        return;
    }

    st->layouts = g_ptr_array_new_with_free_func(g_free);
    st->addable_codes = g_ptr_array_new_with_free_func(g_free);
    load_theme_prefs(st);

    st->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(st->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );

    st->win = gtk_application_window_new(app);
    gtk_widget_add_css_class(st->win, "plume-settings-win");
    gtk_window_set_title(GTK_WINDOW(st->win), "Настройки Plume");
    gtk_window_set_default_size(GTK_WINDOW(st->win), 1040, 700);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(st->win), root_box);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(sidebar, "ps-sidebar");
    gtk_widget_set_size_request(sidebar, 270, -1);
    gtk_box_append(GTK_BOX(root_box), sidebar);

    GtkWidget *sidebar_title = gtk_label_new("Настройки");
    gtk_widget_add_css_class(sidebar_title, "ps-sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(sidebar_title), 0.0f);
    gtk_box_append(GTK_BOX(sidebar), sidebar_title);

    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_add_css_class(search, "ps-nav-search");
    g_object_set(search, "placeholder-text", "Поиск настроек", NULL);
    gtk_box_append(GTK_BOX(sidebar), search);

    GtkWidget *nav = gtk_list_box_new();
    gtk_widget_add_css_class(nav, "ps-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav), GTK_SELECTION_SINGLE);
    add_nav_item(nav, "󰤨", "Wi-Fi", "wifi");
    add_nav_item(nav, "󰈀", "Сеть", "network");
    add_nav_item(nav, "󰂯", "Bluetooth", "bluetooth");
    add_nav_item(nav, "󰍹", "Дисплеи", "displays");
    add_nav_item(nav, "󰕾", "Звук", "sound");
    add_nav_item(nav, "󰁹", "Питание", "power");
    add_nav_item(nav, "󰕮", "Многозадачность", "multitasking");
    add_nav_item(nav, "󰏘", "Внешний вид", "appearance");
    add_nav_item(nav, "󰀻", "Приложения", "applications");
    add_nav_item(nav, "󰂚", "Уведомления", "notifications");
    add_nav_item(nav, "󰍉", "Поиск", "search");
    add_nav_item(nav, "󰀄", "Сетевые учётные записи", "online-accounts");
    add_nav_item(nav, "󰒡", "Благополучие", "wellbeing");
    add_nav_item(nav, "󰍽", "Мышь и сенсорная панель", "mouse");
    add_nav_item(nav, "󰌌", "Клавиатура", "keyboard");
    add_nav_item(nav, "󰏘", "Управление цветом", "color");
    add_nav_item(nav, "󰐪", "Принтеры", "printers");
    add_nav_item(nav, "󰖚", "Специальные возможности", "accessibility");
    add_nav_item(nav, "󰌾", "Конфиденциальность и безопасность", "privacy");
    add_nav_item(nav, "󰒓", "Система", "system");
    add_nav_item(nav, "󰋼", "О Plume", "about");

    GtkWidget *nav_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nav_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(nav_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(nav_scroll), nav);
    gtk_box_append(GTK_BOX(sidebar), nav_scroll);

    st->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(st->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(st->stack), 140);
    gtk_widget_set_hexpand(st->stack, TRUE);
    gtk_widget_set_vexpand(st->stack, TRUE);

    gtk_stack_add_named(GTK_STACK(st->stack), build_wifi_page(st), "wifi");
    gtk_stack_add_named(GTK_STACK(st->stack), build_network_page(st), "network");
    gtk_stack_add_named(GTK_STACK(st->stack), build_bluetooth_page(st), "bluetooth");
    gtk_stack_add_named(GTK_STACK(st->stack), build_displays_page(st), "displays");
    gtk_stack_add_named(GTK_STACK(st->stack), build_sound_page(st), "sound");
    gtk_stack_add_named(GTK_STACK(st->stack), build_power_page(st), "power");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_multitasking_page(st), "multitasking");
    gtk_stack_add_named(GTK_STACK(st->stack), build_appearance_page(st), "appearance");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_applications_page(st), "applications");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Уведомления", "󰂚", "Режим «Не беспокоить» и правила уведомлений приложений."), "notifications");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Поиск", "󰍉", "Источники поиска и параметры индексирования."), "search");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Сетевые учётные записи", "󰀄", "Подключение облачных и сетевых учётных записей."), "online-accounts");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Благополучие", "󰒡", "Экранное время, перерывы и напоминания."), "wellbeing");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Мышь и сенсорная панель", "󰍽", "Скорость указателя, прокрутка, касания и жесты."), "mouse");
    gtk_stack_add_named(GTK_STACK(st->stack), build_keyboard_page(st), "keyboard");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Управление цветом", "󰏘", "Цветовые профили дисплеев и устройств."), "color");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Принтеры", "󰐪", "Добавление принтеров и управление очередью печати."), "printers");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Специальные возможности", "󰖚", "Зрение, слух, ввод и другие параметры доступности."), "accessibility");
    gtk_stack_add_named(GTK_STACK(st->stack), build_placeholder_page(
        "Конфиденциальность и безопасность", "󰌾", "Доступ приложений, блокировка экрана и безопасность системы."), "privacy");
    gtk_stack_add_named(GTK_STACK(st->stack), build_system_page(), "system");
    gtk_stack_add_named(GTK_STACK(st->stack), build_about_page(), "about");
    gtk_box_append(GTK_BOX(root_box), st->stack);

    apply_window_css(st);

    g_signal_connect(nav, "row-selected", G_CALLBACK(on_nav_selected), st);
    g_signal_connect(search, "search-changed", G_CALLBACK(on_nav_search_changed), nav);
    g_signal_connect(st->win, "notify::is-active",
        G_CALLBACK(on_settings_active_changed), st);
    gtk_list_box_select_row(GTK_LIST_BOX(nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(nav), 7));

    gtk_window_present(GTK_WINDOW(st->win));
}

int main(int argc, char **argv) {
    State st = { 0 };
    GtkApplication *app = gtk_application_new("org.ouvert.plume.settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (st.layouts) g_ptr_array_unref(st.layouts);
    if (st.addable_codes) g_ptr_array_unref(st.addable_codes);
    if (st.css_provider) g_object_unref(st.css_provider);
    g_object_unref(app);
    return status;
}
