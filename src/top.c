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
#include <glibtop/loadavg.h>
#include <glibtop/mem.h>
#include <glibtop/swap.h>
#include <glibtop/sysinfo.h>

#include <stdint.h>

G_MODULE_EXPORT Mode mode;

typedef struct
{
    /** Pid of the child. */
    pid_t  pid;
    glibtop_proc_time  time;
    glibtop_proc_mem   mem;
    glibtop_proc_uid   uid;

    guint64 last_rtime;

    double cpu;

    char *command_args;
    /** seen */
    gboolean seen;
} TOPProcessInfo;

typedef enum {
    SORT_MEM     = 0,
    SORT_PID     = 1,
    SORT_TIME    = 2,
    SORT_NAME    = 3,
    SORT_CPU     = 4,
    SORT_ALL     = 5
} TOPSort;

const char *sorting_name[SORT_ALL] = {
    "Memory",
    "Pid",
    "Time",
    "Name",
    "CPU"
};

const char *sorting_order_name[] = {
    "Ascending",
    "Descending",
};

/**
 * The internal data structure holding the private data of the TEST Mode.
 */
typedef struct
{
    const glibtop_sysinfo *sysinfo;
    glibtop_cpu cpu_last;
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

static void load_pid ( pid_t pid, TOPProcessInfo *entry, guint64 passed )
{
    entry->pid = pid;
    glibtop_get_proc_uid  ( &(entry->uid) ,entry->pid );
    glibtop_get_proc_mem  ( &(entry->mem) ,entry->pid );
    glibtop_get_proc_time ( &(entry->time),entry->pid );

    entry->cpu = 0;
    if ( entry->last_rtime > 0 ){
        guint64 time = entry->time.rtime - entry->last_rtime;
        if ( passed > 0 ){
            entry->cpu = (100*time)/(double)passed;
        }

    }

    if ( entry->command_args == NULL ) {
        glibtop_proc_args  args;
        char **argv = glibtop_get_proc_argv ( &(args),entry->pid, 0 );
        if ( argv != NULL ){
            entry->command_args = g_strjoinv ( " ", argv );
            g_strfreev ( argv );
        } else {
            entry->command_args = g_strdup("n/a");
        }
    }

    entry->last_rtime = entry->time.rtime;
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
        case SORT_CPU:
            return (bi->cpu > ai->cpu)? -1 : ((bi->cpu < ai->cpu)? 1: 0);
        default:
            return (bi->mem.rss-bi->mem.share) - (ai->mem.rss - ai->mem.share);
    }
}
static int sorting_pid_t ( gconstpointer a, gconstpointer b, gpointer data)
{
    TOPModePrivateData *rmpd = (TOPModePrivateData*)(data);
    TOPProcessInfo *ai = (TOPProcessInfo*)(a);
    TOPProcessInfo *bi = (TOPProcessInfo*)(b);
    return ai->pid - bi->pid;
}

static pid_t sort_pid_t ( gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer data )
{
    pid_t pa = *((pid_t*)a);
    pid_t pb = *((pid_t*)b);
    return pa -pb;
}


static void get_top (  Mode *sw )
{
    glibtop_cpu cpu_now;
    glibtop_proclist proclistbuf;
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );


    glibtop_get_cpu ( &(cpu_now));
    guint64 passed = cpu_now.total-rmpd->cpu_last.total;

    pid_t *pid_list = glibtop_get_proclist ( &proclistbuf, GLIBTOP_KERN_PROC_ALL|GLIBTOP_EXCLUDE_SYSTEM, 0);


    g_qsort_with_data ( rmpd->array, rmpd->array_length, sizeof(TOPProcessInfo), sorting_pid_t, rmpd);
    g_qsort_with_data ( pid_list, proclistbuf.number, sizeof(pid_t), sort_pid_t, NULL);

    // Mark as not seen
    size_t pi = 0;
    size_t i =0;
    size_t removed = 0,new = 0;
    while ( i < rmpd->array_length  ) {
        rmpd->array[i].seen = FALSE;
        if ( rmpd->array[i].pid == pid_list[pi]) {
            rmpd->array[i].seen = TRUE;
            pid_list[pi] = 0;
            pi++;
            i++;
        }
        else if ( rmpd->array[i].pid > pid_list[pi] ){
            pi++;
        } else {
            free_pid ( &(rmpd->array[i]));
            rmpd->array[i].pid = INT32_MAX;
            i++;
            removed++;
        }
    }
    for ( i = 0; i < proclistbuf.number; i++){
        if ( pid_list[i] > 0 ){
            new++;
        }
    }

    g_qsort_with_data ( rmpd->array, rmpd->array_length, sizeof(TOPProcessInfo), sorting_pid_t, rmpd);
    rmpd->array_length = proclistbuf.number;
    rmpd->array = g_realloc(rmpd->array,sizeof(TOPProcessInfo)*rmpd->array_length);
    for ( pi = 0; pi < proclistbuf.number; pi++){
        if ( pid_list[pi] != 0 ){
            memset ( &(rmpd->array[proclistbuf.number-new]), '\0',sizeof(TOPProcessInfo) );
            rmpd->array[proclistbuf.number-new].pid = pid_list[pi];
            new--;
        }
    }

    for ( size_t i = 0; i < rmpd->array_length;i++)
    {
        load_pid ( rmpd->array[i].pid, &(rmpd->array[i]), passed);
    }

    g_free ( pid_list );

    // Sort it back.
    g_qsort_with_data ( rmpd->array, rmpd->array_length, sizeof(TOPProcessInfo), sorting_info, rmpd);

    rmpd->cpu_last = cpu_now;
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
        pd->sysinfo = glibtop_get_sysinfo();
        pd->sorting = SORT_PID;
        pd->sort_order = TRUE;
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
        retv = RELOAD_DIALOG;
    } else if ( ( mretv & MENU_OK ) ) {
        retv = RELOAD_DIALOG;
    } else if ( ( mretv & MENU_ENTRY_DELETE ) == MENU_ENTRY_DELETE ) {
        if ( selected_line < rmpd->array_length ){
            kill ( rmpd->array[selected_line].pid, SIGTERM);
        }
        retv = RELOAD_DIALOG;
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

static char *node_get_display_string ( const TOPModePrivateData *pd, TOPProcessInfo *info )
{
    guint64  time = info->time.rtime/info->time.frequency;
    unsigned int h = (time/3600);
    unsigned int m = (time/60)%60;
    unsigned int s = (time%60);
    return g_strdup_printf("%5d %5.1f%% %02u:%02u:%02u %6.2fMiB %s",
            info->pid,
            pd->sysinfo->ncpu*info->cpu,
            h,m,s,
            (info->mem.rss-info->mem.share)/(1024*1024.0),
            info->command_args);
}

static char *_get_display_value ( const Mode *sw, unsigned int selected_line, G_GNUC_UNUSED int *state, G_GNUC_UNUSED GList **attr_list, int get_entry )
{
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    return get_entry ? node_get_display_string ( rmpd, &(rmpd->array[selected_line])) : NULL;
}

static int top_token_match ( const Mode *sw, GRegex **tokens, unsigned int index )
{
    TOPModePrivateData *rmpd = (TOPModePrivateData *) mode_get_private_data ( sw );
    return helper_token_match ( tokens, rmpd->array[index].command_args);
}

static char * top_get_message ( const Mode *sw )
{
    const TOPModePrivateData *pd = (const TOPModePrivateData *) mode_get_private_data ( sw );

    glibtop_loadavg buf;
    glibtop_mem mbuf;
    glibtop_swap sbuf;
    glibtop_get_loadavg ( &buf );
    glibtop_get_mem ( &mbuf );
    glibtop_get_swap ( &sbuf );
    char *retv = g_markup_printf_escaped (
            "<b>Sorting:</b> %-10s <b>Load:</b>    %.2lf %.2lf %.2lf\n"\
            "<b>Order:  </b> %-10s <b>Memory:</b> %5.1lf%%            <b>Swap:</b> %5.1lf%%",
                sorting_name[pd->sorting], buf.loadavg[0], buf.loadavg[1], buf.loadavg[2],
                sorting_order_name[pd->sort_order], ((mbuf.used-mbuf.cached-mbuf.buffer)*100)/(double)mbuf.total,
                (sbuf.total>0)?(((sbuf.used)*100)/(double)sbuf.total):0.0
            );
    return retv;
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
    ._get_message       = top_get_message,
    ._get_completion    = NULL,
    ._preprocess_input  = NULL,
    .private_data       = NULL,
    .free               = NULL,
};
