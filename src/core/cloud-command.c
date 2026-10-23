/* Running a short-lived command with captured output and a time limit. */

#include "core/cloud-command.h"

typedef struct
{
    GSubprocess *process;
    gboolean timed_out;
} CommandTimeout;

static void
command_finished (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    GAsyncResult **finished = user_data;

    *finished = g_object_ref (result);
}

static gboolean
command_timed_out (gpointer user_data)
{
    CommandTimeout *timeout = user_data;

    timeout->timed_out = TRUE;
    g_subprocess_force_exit (timeout->process);
    return G_SOURCE_REMOVE;
}

/* A timeout kills the process and still waits for its output to close.
 * Cancelling instead would leave GSubprocess operations pending on the private
 * context, leaking it and its file descriptors. */
gboolean
cloud_command_run (const char * const  *argv,
                   guint                timeout_seconds,
                   char               **standard_output,
                   char               **standard_error,
                   gboolean            *successful,
                   GError             **error)
{
    g_autoptr(GMainContext) context = g_main_context_new ();
    g_autoptr(GSubprocess) process = NULL;
    g_autoptr(GSource) timer = NULL;
    g_autoptr(GAsyncResult) result = NULL;
    g_autoptr(GError) local_error = NULL;
    g_autofree char *output = NULL;
    g_autofree char *errors = NULL;
    CommandTimeout timeout = { NULL, FALSE };
    gboolean completed;

    process = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, error);
    if (process == NULL)
        return FALSE;

    g_main_context_push_thread_default (context);
    timeout.process = process;
    timer = g_timeout_source_new_seconds (timeout_seconds);
    g_source_set_callback (timer, command_timed_out, &timeout, NULL);
    g_source_attach (timer, context);
    g_subprocess_communicate_utf8_async (process, NULL, NULL, command_finished, &result);
    while (result == NULL)
        g_main_context_iteration (context, TRUE);
    g_source_destroy (timer);
    completed = g_subprocess_communicate_utf8_finish (process, result, &output, &errors, &local_error);
    g_main_context_pop_thread_default (context);

    if (timeout.timed_out)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                     "%s did not finish within %u seconds", argv[0], timeout_seconds);
        return FALSE;
    }
    if (!completed)
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    if (standard_output != NULL)
        *standard_output = g_steal_pointer (&output);
    if (standard_error != NULL)
        *standard_error = g_steal_pointer (&errors);
    *successful = g_subprocess_get_successful (process);
    return TRUE;
}
