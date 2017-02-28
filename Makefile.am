ACLOCAL_AMFLAGS=-I m4
plugindir=${libdir}/rofi/

plugin_LTLIBRARIES = top.la

top_la_SOURCES=\
		 src/top.c

top_la_CFLAGS= @glib_CFLAGS@ @rofi_CFLAGS@
top_la_LIBADD= @glib_LIBS@ @rofi_LIBS@
top_la_LDFLAGS= -module -avoid-version
