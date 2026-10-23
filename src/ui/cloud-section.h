/* The Cloud section added to the sidebar: a separator, the "Cloud" heading and
 * one row per location. Emits "activate-location" (CloudLocation *) when a row
 * is clicked. */

#pragma once

#include <gtk/gtk.h>

#include "core/cloud-location.h"

G_BEGIN_DECLS

#define CLOUD_TYPE_SECTION (cloud_section_get_type ())

G_DECLARE_FINAL_TYPE (CloudSection, cloud_section, CLOUD, SECTION, GtkBox)

void           cloud_section_register_types (GTypeModule *module);

GtkWidget     *cloud_section_new (void);

/* Shows one row per location (CloudLocation, in order), and hides the section
 * when there are none. Keeps a reference to LOCATIONS, which must not change. */
void           cloud_section_set_locations (CloudSection *self,
                                            GPtrArray    *locations);
guint          cloud_section_get_n_rows (CloudSection *self);

/* Selects LOCATION's row, or unselects all rows if LOCATION is NULL. */
void           cloud_section_select (CloudSection  *self,
                                     CloudLocation *location);
CloudLocation *cloud_section_get_selected (CloudSection *self);

/* The list box holding the rows. */
GtkWidget     *cloud_section_get_list (CloudSection *self);

/* Greys the rows out, e.g. while a drag they can't accept is over the sidebar. */
void           cloud_section_set_dimmed (CloudSection *self,
                                         gboolean      dimmed);

/* Gives the heading the font and colour of LABEL. */
void           cloud_section_copy_heading_style (CloudSection *self,
                                                 GtkWidget    *label);

G_END_DECLS
