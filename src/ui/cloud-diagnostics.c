/* Optional runtime report of the in-process widgets. */

#include "ui/cloud-diagnostics.h"

#include <json-glib/json-glib.h>

#include "core/cloud-location.h"
#include "ui/cloud-window.h"

#define DIAGNOSTICS_VARIABLE "NAUTILUS_CLOUD_DIAGNOSTICS"

static void
add_window (JsonBuilder *builder,
            CloudWindow *window,
            GPtrArray   *locations)
{
    CloudSection *section = cloud_window_get_section (window);
    CloudLocation *selected = cloud_section_get_selected (section);
    const char *location = cloud_window_get_location_uri (window);

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "heading");
    json_builder_add_string_value (builder, "Cloud");
    json_builder_set_member_name (builder, "names");
    json_builder_begin_array (builder);
    for (guint i = 0; i < locations->len; i++)
        json_builder_add_string_value (builder, ((CloudLocation *) g_ptr_array_index (locations, i))->name);
    json_builder_end_array (builder);
    json_builder_set_member_name (builder, "rows");
    json_builder_add_int_value (builder, cloud_section_get_n_rows (section));
    json_builder_set_member_name (builder, "hidden_duplicates");
    json_builder_add_int_value (builder, cloud_window_get_hidden_duplicates (window));
    json_builder_set_member_name (builder, "location");
    if (location != NULL)
        json_builder_add_string_value (builder, location);
    else
        json_builder_add_null_value (builder);
    json_builder_set_member_name (builder, "selected");
    json_builder_begin_array (builder);
    if (selected != NULL)
        json_builder_add_string_value (builder, selected->name);
    json_builder_end_array (builder);
    json_builder_end_object (builder);
}

void
cloud_diagnostics_report (GPtrArray *windows,
                          GPtrArray *locations)
{
    const char *destination = g_getenv (DIAGNOSTICS_VARIABLE);
    g_autoptr(JsonBuilder) builder = NULL;
    g_autoptr(JsonNode) root = NULL;
    g_autoptr(JsonGenerator) generator = NULL;
    g_autofree char *text = NULL;
    g_autoptr(GError) error = NULL;

    if (destination == NULL || *destination == '\0')
        return;
    builder = json_builder_new ();
    json_builder_begin_array (builder);
    for (guint i = 0; i < windows->len; i++)
    {
        CloudWindow *window = g_ptr_array_index (windows, i);

        if (cloud_window_get_window (window) != NULL)
            add_window (builder, window, locations);
    }
    json_builder_end_array (builder);

    root = json_builder_get_root (builder);
    generator = json_generator_new ();
    json_generator_set_root (generator, root);
    json_generator_set_pretty (generator, TRUE);
    json_generator_set_indent (generator, 2);
    text = json_generator_to_data (generator, NULL);
    if (!g_file_set_contents (destination, text, -1, &error))
        g_warning ("Could not write Cloud sidebar diagnostics: %s", error->message);
}
