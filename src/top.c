/**
 * rofi-top
 *
 * MIT/X11 License
 * Copyright (c) 2017 Qball Cow <qball@gmpclient.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <gmodule.h>

#include <sys/types.h>
#include <signal.h>

#include <rofi/mode.h>
#include <rofi/helper.h>
#include <rofi/mode-private.h>

#include <glibtop.h>
#include <glibtop/proclist.h>
#include <glibtop/procargs.h>
#include <glibtop/proctime.h>
#include <glibtop/procmem.h>
#include <glibtop/procuid.h>

G_MODULE_EXPORT Mode mode;

typedef struct
{
    /** Pid of the child. */
    pid_t  pid;
    glibtop_proc_time  time;
    glibtop_proc_mem   mem;
    glibtop_proc_uid   uid;

    char *command_args;
} TOPProcessInfo;

typedef enum {
    SORT_MEM     = 0,
    SORT_PID     = 1,
    SORT_TIME    = 2,
    SORT_NAME    = 3,
    SORT_ALL     = 4
} TOPSort;

const char *topsort[SORT_ALL] = {
    "Memory",
    "Pid",
    "Time",
    "Name",
};

/**
 * The internal data structure holding the private data of the TEST Mode.
 */
typedef struct
{
    TOPSort sorting;
    gboolean sort_order;
    guint timeout;
    /** List of processes */
    TOPProcessInfo *array;
    size_t          array_length;
} TOPModePrivateData;

static void free_pid ( TOPProcessInfo *entry )
{
    g_free ( entry->command_args );
}

static void load_pid ( pid_t pid, TOPProcessInfo *entry )
{
    entry->pid = pid;
    glibtop_get_proc_uid  ( &(entry->uid) ,entry->pid );
    glibtop_get_proc_mem  ( &(entry->mem) ,entry->pid );
    glibtop_get_proc_time ( &(entry->time),entry->pid );

    glibtop_proc_args  args;
    char *arg = glibtop_get_proc_args ( &(args),entry->pid, 0 );
    for ( size_t i = 0; i < (args.size-1); i++){
        if ( arg[i] == '\0' ){
            arg[i] = ' ';
        }
    }
    printf("arg: %s\n",arg);
    entry->command_args = g_strdup(arg);//g_path_get_basename ( arg );

    g_free ( arg );
}

static int sorting_info ( gconstpointer a, gconstpointer b, gpointer data)
{
    TOPModePrivateData *rmpd = (TOPModePrivateData*)(data);
    TOPProcessInfo *ai = (TOPProcessInfo*)((rmpd->sort_order)?a:b);
    TOPProcessInfo *bi = (TOPProcessInfo*)((rmpd->sort_order)?b:a);
    switch ( rmpd->sorting ){
        case SORT_PID:
            return bi->pid - ai->pid;
        case SORT_TIME:
            return (bi->time.rtime) - (ai->time.rtime);
        case SORT_NAME:
            return g_strcmp0(bi->command_args, ai->command_args);
        default:
            return bi->mem.rss-ai->mem.rss;
    }
}

static void get_top (  Mode *sw )
{
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    for ( size_t i = 0; i < rmpd->array_length; i++ ) {
        free_pid ( &(rmpd->array[i]) );
    }
    g_free ( rmpd->array );
    rmpd->array        = NULL;
    rmpd->array_length = 0;

    glibtop_proclist proclistbuf;
    pid_t *pid_list = glibtop_get_proclist ( &proclistbuf, GLIBTOP_KERN_PROC_ALL|GLIBTOP_EXCLUDE_SYSTEM, 0);
    rmpd->array_length = proclistbuf.number;
    rmpd->array = g_malloc0(sizeof(TOPProcessInfo)*rmpd->array_length);
    for ( size_t i = 0; i < rmpd->array_length;i++)
    {
        load_pid ( pid_list[i], &(rmpd->array[i]));
    }

    g_free ( pid_list );

    g_qsort_with_data ( rmpd->array, rmpd->array_length, sizeof(TOPProcessInfo), sorting_info, rmpd);
}

static gboolean timeout_function ( gpointer data )
{
    Mode *sw = (Mode *) data;
    get_top ( sw );

    rofi_view_reload ();
    return G_SOURCE_CONTINUE;
}

static int top_mode_init ( Mode *sw )
{
    if ( mode_get_private_data ( sw ) == NULL ) {
        TOPModePrivateData *pd = g_malloc0 ( sizeof ( *pd ) );
        mode_set_private_data ( sw, (void *) pd );

        pd->timeout = g_timeout_add_seconds ( 1, timeout_function, sw );
        get_top ( sw );
    }
    return TRUE;
}
static unsigned int top_mode_get_num_entries ( const Mode *sw )
{
    const TOPModePrivateData *rmpd = (const TOPModePrivateData *) mode_get_private_data ( sw );
    return rmpd->array_length;
}

static ModeMode top_mode_result ( Mode *sw, int mretv, char **input, unsigned int selected_line )
{
    ModeMode           retv  = MODE_EXIT;
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    if ( mretv & MENU_NEXT ) {
        retv = NEXT_DIALOG;
    } else if ( mretv & MENU_PREVIOUS ) {
        retv = PREVIOUS_DIALOG;
    } else if ( mretv & MENU_QUICK_SWITCH ) {
        retv = ( mretv & MENU_LOWER_MASK );
        int new_sort = retv%SORT_ALL;
        if ( new_sort != rmpd->sorting ){
            rmpd->sorting = new_sort;
        } else {
            rmpd->sort_order = !(rmpd->sort_order);
        }
        retv = RESET_DIALOG;
    } else if ( ( mretv & MENU_OK ) ) {
        retv = RESET_DIALOG;
    } else if ( ( mretv & MENU_ENTRY_DELETE ) == MENU_ENTRY_DELETE ) {
        if ( selected_line < rmpd->array_length ){
            kill ( rmpd->array[selected_line].pid, SIGTERM);
        }
        retv = RESET_DIALOG;
    }
    return retv;
}

static void top_mode_destroy ( Mode *sw )
{
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    if ( rmpd != NULL ) {
        g_source_remove ( rmpd->timeout );
        g_free ( rmpd );
        mode_set_private_data ( sw, NULL );
    }
}

static char *node_get_display_string ( TOPProcessInfo *info )
{
    return g_strdup_printf("%5d %02lu:%02lu:%02lu %6.2fMiB %s",
            info->pid,
            (info->time.rtime/3600),
            (info->time.rtime/60)%60,
            info->time.rtime%60,
            (info->mem.rss-info->mem.share)/(1024*1024.0),
            info->command_args);
}

static char *_get_display_value ( const Mode *sw, unsigned int selected_line, G_GNUC_UNUSED int *state, int get_entry )
{
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    return get_entry ? node_get_display_string ( &(rmpd->array[selected_line])) : NULL;
}

static int top_token_match ( const Mode *sw, GRegex **tokens, unsigned int index )
{
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    return helper_token_match ( tokens, rmpd->array[index].command_args);
}

Mode mode =
{
    .abi_version        = ABI_VERSION,
    .name               = "top",
    .cfg_name_key       = "display-top",
    ._init              = top_mode_init,
    ._get_num_entries   = top_mode_get_num_entries,
    ._result            = top_mode_result,
    ._destroy           = top_mode_destroy,
    ._token_match       = top_token_match,
    ._get_display_value = _get_display_value,
    ._get_completion    = NULL,
    ._preprocess_input  = NULL,
    .private_data       = NULL,
    .free               = NULL,
};
