/* Entry points Nautilus calls when it loads the module. */

#include <nautilus-extension.h>

#include "cloud-extension.h"
#include "services/cloud-scanner.h"
#include "ui/cloud-drag-tracker.h"
#include "ui/cloud-section.h"
#include "ui/cloud-window.h"

static GType extension_types[1];

/* Every GObject type lives in Nautilus' GTypeModule, as loadable modules require. */
G_MODULE_EXPORT void
nautilus_module_initialize (GTypeModule *module)
{
    cloud_scanner_register_types (module);
    cloud_drag_tracker_register_types (module);
    cloud_section_register_types (module);
    cloud_window_register_types (module);
    cloud_sidebar_extension_register_types (module);
    extension_types[0] = CLOUD_TYPE_SIDEBAR_EXTENSION;
}

G_MODULE_EXPORT void
nautilus_module_shutdown (void)
{
}

/* Nautilus creates one instance of each listed type for the life of the process. */
G_MODULE_EXPORT void
nautilus_module_list_types (const GType **types,
                            int          *num_types)
{
    *types = extension_types;
    *num_types = G_N_ELEMENTS (extension_types);
}
