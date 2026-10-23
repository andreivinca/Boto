/* The Nautilus extension object: finds Files windows, adds a Cloud section to
 * each, and keeps them all showing the scanner's current locations. */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define CLOUD_TYPE_SIDEBAR_EXTENSION (cloud_sidebar_extension_get_type ())

G_DECLARE_FINAL_TYPE (CloudSidebarExtension, cloud_sidebar_extension, CLOUD, SIDEBAR_EXTENSION, GObject)

void cloud_sidebar_extension_register_types (GTypeModule *module);

G_END_DECLS
