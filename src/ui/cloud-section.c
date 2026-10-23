/* The Cloud section added to the sidebar. */

#include "ui/cloud-section.h"

#include "nautilus/cloud-nautilus.h"

#define ROW_ICON "folder-remote-symbolic"

/* The section's own look, over the theme's sidebar style. The list drops the
 * theme's top padding so the heading has as much space below it as above. */
#define SECTION_CSS                                                                 \
    "label#" CLOUD_HEADING_NAME " {"                                                \
    "  font-size: 0.82em; font-weight: bold; color: alpha(currentColor, 0.4);"      \
    "}"                                                                             \
    "list#" CLOUD_LIST_NAME " { padding-top: 0; }"

/* Column of the row icons: the theme's row margin (6) plus row padding (14). */
#define HEADING_INDENT 20

struct _CloudSection
{
    GtkBox parent_instance;

    GtkListBox *list;
    GtkCssProvider *heading_css;
    char *heading_style;
    GPtrArray *locations;   /* row i shows locations[i] */
};

enum
{
    ACTIVATE_LOCATION,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_DYNAMIC_TYPE_EXTENDED (CloudSection, cloud_section, GTK_TYPE_BOX, G_TYPE_FLAG_FINAL, {})

static CloudLocation *
location_at (CloudSection *self,
             int           index)
{
    if (self->locations == NULL || index < 0 || (guint) index >= self->locations->len)
        return NULL;
    return g_ptr_array_index (self->locations, index);
}

static void
row_activated (GtkListBox    *list,
               GtkListBoxRow *row,
               CloudSection  *self)
{
    CloudLocation *location = location_at (self, gtk_list_box_row_get_index (row));

    if (location != NULL)
        g_signal_emit (self, signals[ACTIVATE_LOCATION], 0, location);
}

/* Built like Nautilus' own rows (nautilus-sidebar-row.ui), so the theme lays
 * both out identically: the revealer gets the sidebar's row padding, and the
 * row its height. */
static GtkWidget *
create_row (CloudLocation *location)
{
    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *revealer = gtk_revealer_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *icon = gtk_image_new_from_icon_name (ROW_ICON);
    GtkWidget *label = gtk_label_new (location->name);

    gtk_widget_set_tooltip_text (row, location->name);
    gtk_widget_set_focus_on_click (row, FALSE);
    gtk_widget_add_css_class (revealer, "sidebar-revealer");
    gtk_revealer_set_transition_type (GTK_REVEALER (revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
    gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), TRUE);
    gtk_image_set_pixel_size (GTK_IMAGE (icon), 16);
    gtk_widget_set_margin_end (icon, 8);
    gtk_box_append (GTK_BOX (box), icon);
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_margin_end (label, 2);
    gtk_box_append (GTK_BOX (box), label);
    gtk_revealer_set_child (GTK_REVEALER (revealer), box);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), revealer);
    return row;
}

void
cloud_section_set_locations (CloudSection *self,
                             GPtrArray    *locations)
{
    g_return_if_fail (CLOUD_IS_SECTION (self));

    gtk_list_box_remove_all (self->list);
    g_clear_pointer (&self->locations, g_ptr_array_unref);
    self->locations = g_ptr_array_ref (locations);
    for (guint i = 0; i < locations->len; i++)
        gtk_list_box_append (self->list, create_row (g_ptr_array_index (locations, i)));
    gtk_widget_set_visible (GTK_WIDGET (self), locations->len > 0);
}

guint
cloud_section_get_n_rows (CloudSection *self)
{
    g_return_val_if_fail (CLOUD_IS_SECTION (self), 0);

    return self->locations ? self->locations->len : 0;
}

void
cloud_section_select (CloudSection  *self,
                      CloudLocation *location)
{
    guint index;

    g_return_if_fail (CLOUD_IS_SECTION (self));

    if (location != NULL && self->locations != NULL && g_ptr_array_find (self->locations, location, &index))
        gtk_list_box_select_row (self->list, gtk_list_box_get_row_at_index (self->list, index));
    else
        gtk_list_box_unselect_all (self->list);
}

CloudLocation *
cloud_section_get_selected (CloudSection *self)
{
    GtkListBoxRow *row;

    g_return_val_if_fail (CLOUD_IS_SECTION (self), NULL);

    row = gtk_list_box_get_selected_row (self->list);
    return row ? location_at (self, gtk_list_box_row_get_index (row)) : NULL;
}

GtkWidget *
cloud_section_get_list (CloudSection *self)
{
    g_return_val_if_fail (CLOUD_IS_SECTION (self), NULL);

    return GTK_WIDGET (self->list);
}

/* Insensitive, as Nautilus marks its own rows that can't take a drop. */
void
cloud_section_set_dimmed (CloudSection *self,
                          gboolean      dimmed)
{
    g_return_if_fail (CLOUD_IS_SECTION (self));

    gtk_widget_set_sensitive (GTK_WIDGET (self->list), !dimmed);
}

static void
append_css_string (GString    *css,
                   const char *value)
{
    g_string_append_c (css, '"');
    for (const char *p = value; *p != '\0'; p++)
    {
        if (*p == '"' || *p == '\\')
            g_string_append_printf (css, "\\%c", *p);
        else if ((guchar) *p < 0x20 || *p == 0x7f)
            g_string_append_printf (css, "\\%x ", (guchar) *p);
        else
            g_string_append_c (css, *p);
    }
    g_string_append_c (css, '"');
}

void
cloud_section_copy_heading_style (CloudSection *self,
                                  GtkWidget    *label)
{
    const PangoFontDescription *font;
    const char *family;
    char size[G_ASCII_DTOSTR_BUF_SIZE];
    GdkRGBA rgba;
    g_autofree char *color = NULL;
    g_autoptr(GString) css = g_string_new ("label { ");

    g_return_if_fail (CLOUD_IS_SECTION (self));

    font = pango_context_get_font_description (gtk_widget_get_pango_context (label));
    family = pango_font_description_get_family (font);
    g_ascii_dtostr (size, sizeof size, (double) pango_font_description_get_size (font) / PANGO_SCALE);
    gtk_widget_get_color (label, &rgba);
    color = gdk_rgba_to_string (&rgba);
    if (family != NULL)
    {
        g_string_append (css, "font-family: ");
        append_css_string (css, family);
        g_string_append (css, "; ");
    }
    g_string_append_printf (css, "font-size: %s%s; font-weight: %d; color: %s; }",
                            size, pango_font_description_get_size_is_absolute (font) ? "px" : "pt",
                            (int) pango_font_description_get_weight (font), color);
    if (g_strcmp0 (css->str, self->heading_style) == 0)
        return;
    gtk_css_provider_load_from_string (self->heading_css, css->str);
    g_free (self->heading_style);
    self->heading_style = g_strdup (css->str);
}

GtkWidget *
cloud_section_new (void)
{
    return g_object_new (CLOUD_TYPE_SECTION, NULL);
}

/* Once per display; the rules only match this section's named widgets. */
static void
add_stylesheet (GdkDisplay *display)
{
    static GdkDisplay *styled;
    g_autoptr(GtkCssProvider) provider = NULL;

    if (display == NULL || display == styled)
        return;
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (provider, SECTION_CSS);
    gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    styled = display;
}

static void
cloud_section_init (CloudSection *self)
{
    GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *heading = gtk_label_new ("Cloud");

    add_stylesheet (gtk_widget_get_display (GTK_WIDGET (self)));
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_name (GTK_WIDGET (self), CLOUD_SECTION_NAME);

    gtk_widget_set_margin_start (separator, 12);
    gtk_widget_set_margin_end (separator, 12);
    gtk_widget_set_margin_top (separator, 6);
    gtk_widget_set_margin_bottom (separator, 6);
    gtk_box_append (GTK_BOX (self), separator);

    gtk_label_set_xalign (GTK_LABEL (heading), 0);
    gtk_widget_set_name (heading, CLOUD_HEADING_NAME);
    gtk_widget_set_margin_start (heading, HEADING_INDENT);
    gtk_widget_set_margin_top (heading, 4);
    /* Empty until cloud_section_copy_heading_style(), which overrides SECTION_CSS.
     * GTK 4 has no replacement for a per-widget style provider. */
    self->heading_css = gtk_css_provider_new ();
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_style_context_add_provider (gtk_widget_get_style_context (heading),
                                    GTK_STYLE_PROVIDER (self->heading_css),
                                    GTK_STYLE_PROVIDER_PRIORITY_USER);
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_box_append (GTK_BOX (self), heading);

    self->list = GTK_LIST_BOX (gtk_list_box_new ());
    gtk_widget_set_name (GTK_WIDGET (self->list), CLOUD_LIST_NAME);
    gtk_widget_add_css_class (GTK_WIDGET (self->list), "navigation-sidebar");
    gtk_list_box_set_selection_mode (self->list, GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click (self->list, TRUE);
    g_signal_connect (self->list, "row-activated", G_CALLBACK (row_activated), self);
    gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->list));
}

static void
cloud_section_dispose (GObject *object)
{
    CloudSection *self = CLOUD_SECTION (object);

    g_clear_pointer (&self->locations, g_ptr_array_unref);
    g_clear_object (&self->heading_css);
    G_OBJECT_CLASS (cloud_section_parent_class)->dispose (object);
}

static void
cloud_section_finalize (GObject *object)
{
    g_free (CLOUD_SECTION (object)->heading_style);
    G_OBJECT_CLASS (cloud_section_parent_class)->finalize (object);
}

static void
cloud_section_class_init (CloudSectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = cloud_section_dispose;
    object_class->finalize = cloud_section_finalize;

    signals[ACTIVATE_LOCATION] = g_signal_new ("activate-location", G_TYPE_FROM_CLASS (klass),
                                               G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                               G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
cloud_section_class_finalize (CloudSectionClass *klass)
{
}

void
cloud_section_register_types (GTypeModule *module)
{
    cloud_section_register_type (module);
}
