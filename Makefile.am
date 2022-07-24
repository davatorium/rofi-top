ACLOCAL_AMFLAGS=-I m4
plugindir=@rofi_PLUGIN_INSTALL_DIR@

plugin_LTLIBRARIES = top.la

top_la_SOURCES=\
     src/top.c

top_la_CFLAGS= @glib_CFLAGS@ @rofi_CFLAGS@ @cairo_CFLAGS@
top_la_LIBADD= @glib_LIBS@ @rofi_LIBS@ @cairo_LIBS@
top_la_LDFLAGS= -module -avoid-version
