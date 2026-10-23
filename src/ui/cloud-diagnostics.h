/* Optional runtime report of the in-process widgets.
 *
 * When NAUTILUS_CLOUD_DIAGNOSTICS names a file, it is rewritten after each
 * refresh with one JSON object per Files window:
 * {"heading", "names", "rows", "hidden_duplicates", "location", "selected"}.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* WINDOWS: CloudWindow, in injection order. LOCATIONS: CloudLocation. */
void cloud_diagnostics_report (GPtrArray *windows,
                               GPtrArray *locations);

G_END_DECLS
