#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GtkWidget *win;
    GtkWidget *stack;
    GtkWidget *wall_pic;
    GtkWidget *wall_path_lbl;
    GtkWidget *dd_layout;
    GtkWidget *dd_toggle;
    GtkWidget *kbd_status_lbl;
    gboolean loading;
} State;

static const char *const LAYOUT_IDS[] = { "us,ru", "us", "us,ru,ua", NULL };
static const char *const LAYOUT_LABELS[] = {
    "Английская + Русская (US, RU)",
    "Только английская (US)",
    "Английская + Русская + Украинская (US, RU, UA)",
    NULL
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

    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
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
    if (err) {
        g_error_free(err);
        return;
    }
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

static void load_keyboard_prefs(State *st) {
    guint l_idx = 0;
    guint t_idx = 0;
    char *cfg = get_config_dir();
    char *kf = g_build_filename(cfg, "keyboard.env", NULL);
    g_free(cfg);

    char *content = NULL;
    if (g_file_get_contents(kf, &content, NULL, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "XKB_DEFAULT_LAYOUT=")) {
                const char *v = lines[i] + strlen("XKB_DEFAULT_LAYOUT=");
                for (guint j = 0; LAYOUT_IDS[j]; j++) {
                    if (strcmp(v, LAYOUT_IDS[j]) == 0) { l_idx = j; break; }
                }
            } else if (g_str_has_prefix(lines[i], "XKB_DEFAULT_OPTIONS=")) {
                const char *v = lines[i] + strlen("XKB_DEFAULT_OPTIONS=");
                for (guint j = 0; TOGGLE_IDS[j]; j++) {
                    if (strcmp(v, TOGGLE_IDS[j]) == 0) { t_idx = j; break; }
                }
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(kf);

    st->loading = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->dd_layout), l_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->dd_toggle), t_idx);
    st->loading = FALSE;
}

static void on_kbd_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    State *st = user_data;
    if (st->loading) return;

    guint l_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->dd_layout));
    guint t_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->dd_toggle));
    if (l_idx > 2) l_idx = 0;
    if (t_idx > 3) t_idx = 0;

    const char *layout = LAYOUT_IDS[l_idx];
    const char *toggle = TOGGLE_IDS[t_idx];

    char *cfg = get_config_dir();
    char *kf = g_build_filename(cfg, "keyboard.env", NULL);
    g_free(cfg);

    char *body = g_strdup_printf(
        "XKB_DEFAULT_LAYOUT=%s\nXKB_DEFAULT_OPTIONS=%s\n",
        layout, toggle
    );
    g_file_set_contents(kf, body, -1, NULL);
    g_free(kf);

    const char *rt = getenv("PLUME_RUNTIME");
    if (rt && *rt) {
        char *rt_env = g_build_filename(rt, "labwc", "environment", NULL);
        char *rt_body = g_strdup_printf("%sGTK_THEME=Plume\n", body);
        g_file_set_contents(rt_env, rt_body, -1, NULL);
        g_free(rt_body);
        g_free(rt_env);

        char *argv[] = { (char *)"labwc", (char *)"--reconfigure", NULL };
        GError *err = NULL;
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
        if (err) g_error_free(err);
    }
    g_free(body);

    gtk_label_set_text(GTK_LABEL(st->kbd_status_lbl), "Сохранено и применено в labwc");
}

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

static GtkWidget *build_wallpaper_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Обои рабочего стола"));

    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "ps-preview-wrap");
    st->wall_pic = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(st->wall_pic), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_size_request(st->wall_pic, 440, 230);
    gtk_box_append(GTK_BOX(frame), st->wall_pic);
    gtk_box_append(GTK_BOX(box), frame);

    st->wall_path_lbl = gtk_label_new("");
    gtk_widget_add_css_class(st->wall_path_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->wall_path_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(st->wall_path_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(box), st->wall_path_lbl);

    char *cur = read_current_wallpaper();
    if (g_file_test(cur, G_FILE_TEST_IS_REGULAR)) {
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
    gtk_box_append(GTK_BOX(box), btns);

    return box;
}

static GtkWidget *build_keyboard_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Клавиатура и раскладки"));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *l1 = gtk_label_new("Раскладки клавиатуры");
    gtk_widget_add_css_class(l1, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
    st->dd_layout = gtk_drop_down_new_from_strings(LAYOUT_LABELS);
    gtk_box_append(GTK_BOX(card), l1);
    gtk_box_append(GTK_BOX(card), st->dd_layout);

    GtkWidget *l2 = gtk_label_new("Комбинация переключения");
    gtk_widget_add_css_class(l2, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
    st->dd_toggle = gtk_drop_down_new_from_strings(TOGGLE_LABELS);
    gtk_box_append(GTK_BOX(card), l2);
    gtk_box_append(GTK_BOX(card), st->dd_toggle);

    gtk_box_append(GTK_BOX(box), card);

    st->kbd_status_lbl = gtk_label_new("Изменения сохраняются в ~/.config/plume/keyboard.env");
    gtk_widget_add_css_class(st->kbd_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->kbd_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), st->kbd_status_lbl);

    load_keyboard_prefs(st);
    g_signal_connect(st->dd_layout, "notify::selected", G_CALLBACK(on_kbd_changed), st);
    g_signal_connect(st->dd_toggle, "notify::selected", G_CALLBACK(on_kbd_changed), st);

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
    gtk_box_append(GTK_BOX(card), make_info_row("Композитор", "labwc (wlroots · Wayland)"));
    gtk_box_append(GTK_BOX(card), make_info_row("Компоненты", "Waybar · Mako · GTK4"));
    gtk_box_append(GTK_BOX(card), make_info_row("Репозиторий", "github.com/etern4llyfunny/plume"));
    g_free(os_name);

    gtk_box_append(GTK_BOX(box), card);
    return box;
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
    gtk_list_box_append(GTK_LIST_BOX(list), row);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    State *st = user_data;
    if (st->win) {
        gtk_window_present(GTK_WINDOW(st->win));
        return;
    }

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "window.plume-settings-win {"
        "  background-color: #171a21;"
        "  color: #edf0f5;"
        "  font-family: 'Onest', 'Sora', 'JetBrainsMono Nerd Font', 'Symbols Nerd Font', sans-serif;"
        "  font-size: 13px;"
        "}"
        "box.ps-sidebar {"
        "  background-color: #13161c;"
        "  border-right: 1px solid #252b37;"
        "  padding: 12px 8px;"
        "}"
        "list.ps-nav {"
        "  background: transparent;"
        "}"
        "list.ps-nav row {"
        "  padding: 9px 12px;"
        "  margin: 2px 0;"
        "  border-radius: 6px;"
        "  color: #cfd5df;"
        "}"
        "list.ps-nav row:hover {"
        "  background-color: #1c212b;"
        "  color: #edf0f5;"
        "}"
        "list.ps-nav row:selected {"
        "  background-color: #2f7fe6;"
        "  color: #ffffff;"
        "}"
        ".ps-nav-icon {"
        "  font-size: 15px;"
        "  min-width: 18px;"
        "}"
        "box.ps-page {"
        "  padding: 20px 24px;"
        "}"
        ".ps-title {"
        "  font-size: 18px;"
        "  font-weight: 600;"
        "  color: #edf0f5;"
        "}"
        ".ps-hero-title {"
        "  font-size: 20px;"
        "  font-weight: 600;"
        "  color: #edf0f5;"
        "}"
        ".ps-card {"
        "  background-color: #13161c;"
        "  border: 1px solid #252b37;"
        "  border-radius: 8px;"
        "  padding: 14px 16px;"
        "}"
        ".ps-card-title {"
        "  font-weight: 600;"
        "  color: #edf0f5;"
        "}"
        ".ps-muted {"
        "  color: #8b96a8;"
        "  font-size: 12px;"
        "}"
        ".ps-tool-icon {"
        "  font-size: 20px;"
        "  color: #5aa0f5;"
        "  min-width: 28px;"
        "}"
        ".ps-preview-wrap {"
        "  background-color: #13161c;"
        "  border: 1px solid #252b37;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
        "button.ps-btn {"
        "  background: #1c212b;"
        "  background-image: none;"
        "  color: #edf0f5;"
        "  border: 1px solid #2b3342;"
        "  border-radius: 6px;"
        "  padding: 7px 14px;"
        "  box-shadow: none;"
        "}"
        "button.ps-btn:hover {"
        "  background: #252b37;"
        "  background-image: none;"
        "  border-color: #2f7fe6;"
        "}"
        "button.ps-btn-primary {"
        "  background: #2f7fe6;"
        "  background-image: none;"
        "  color: #ffffff;"
        "  border: 1px solid #2f7fe6;"
        "  border-radius: 6px;"
        "  padding: 7px 14px;"
        "  box-shadow: none;"
        "}"
        "button.ps-btn-primary:hover {"
        "  background: #3d8bf0;"
        "  background-image: none;"
        "}"
        "dropdown > button {"
        "  background: #1c212b;"
        "  background-image: none;"
        "  color: #edf0f5;"
        "  border: 1px solid #2b3342;"
        "  border-radius: 6px;"
        "  padding: 7px 12px;"
        "  box-shadow: none;"
        "}"
        "dropdown > button:hover {"
        "  background: #252b37;"
        "  background-image: none;"
        "  border-color: #2f7fe6;"
        "}"
        "popover > contents {"
        "  background: #171a21;"
        "  background-image: none;"
        "  border: 1px solid #2b3342;"
        "  border-radius: 6px;"
        "  color: #edf0f5;"
        "  padding: 4px;"
        "}"
        "popover listview {"
        "  background: transparent;"
        "  color: #edf0f5;"
        "}"
        "popover listview > row {"
        "  padding: 6px 10px;"
        "  border-radius: 4px;"
        "  color: #edf0f5;"
        "}"
        "popover listview > row:hover, popover listview > row:selected {"
        "  background: #2f7fe6;"
        "  color: #ffffff;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );
    g_object_unref(css);

    st->win = gtk_application_window_new(app);
    gtk_widget_add_css_class(st->win, "plume-settings-win");
    gtk_window_set_title(GTK_WINDOW(st->win), "Настройки Plume");
    gtk_window_set_default_size(GTK_WINDOW(st->win), 720, 480);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(st->win), root_box);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(sidebar, "ps-sidebar");
    gtk_widget_set_size_request(sidebar, 195, -1);
    gtk_box_append(GTK_BOX(root_box), sidebar);

    GtkWidget *nav = gtk_list_box_new();
    gtk_widget_add_css_class(nav, "ps-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav), GTK_SELECTION_SINGLE);
    add_nav_item(nav, "󰸉", "Обои", "wallpaper");
    add_nav_item(nav, "󰌌", "Клавиатура", "keyboard");
    add_nav_item(nav, "󰒓", "Система", "system");
    add_nav_item(nav, "󰋼", "О Plume", "about");
    gtk_box_append(GTK_BOX(sidebar), nav);

    st->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(st->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(st->stack), 140);
    gtk_widget_set_hexpand(st->stack, TRUE);
    gtk_widget_set_vexpand(st->stack, TRUE);

    gtk_stack_add_named(GTK_STACK(st->stack), build_wallpaper_page(st), "wallpaper");
    gtk_stack_add_named(GTK_STACK(st->stack), build_keyboard_page(st), "keyboard");
    gtk_stack_add_named(GTK_STACK(st->stack), build_system_page(), "system");
    gtk_stack_add_named(GTK_STACK(st->stack), build_about_page(), "about");
    gtk_box_append(GTK_BOX(root_box), st->stack);

    g_signal_connect(nav, "row-selected", G_CALLBACK(on_nav_selected), st);
    gtk_list_box_select_row(GTK_LIST_BOX(nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(nav), 0));

    gtk_window_present(GTK_WINDOW(st->win));
}

int main(int argc, char **argv) {
    State st = { 0 };
    GtkApplication *app = gtk_application_new("org.ouvert.plume.settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
