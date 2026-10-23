/* Running a short-lived command with captured output and a time limit. */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* Runs ARGV with stdin on /dev/null and the inherited environment, blocking the
 * calling thread. Returns FALSE with ERROR (G_IO_ERROR_TIMED_OUT after
 * TIMEOUT_SECONDS) if the command could not run to completion; otherwise sets
 * SUCCESSFUL to whether it exited with status 0. The outputs may be NULL. */
gboolean cloud_command_run (const char * const  *argv,
                            guint                timeout_seconds,
                            char               **standard_output,
                            char               **standard_error,
                            gboolean            *successful,
                            GError             **error);

G_END_DECLS
