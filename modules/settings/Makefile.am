NULL =

module_LTLIBRARIES = module-settings.la

module_settings_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"evolution-module-settings\" \
	$(EVOLUTION_DATA_SERVER_CFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(GTKHTML_CFLAGS) \
	$(NULL)

module_settings_la_SOURCES = \
	evolution-module-settings.c \
	e-settings-cal-model.c \
	e-settings-cal-model.h \
	e-settings-calendar-item.c \
	e-settings-calendar-item.h \
	e-settings-calendar-view.c \
	e-settings-calendar-view.h \
	e-settings-client-cache.c \
	e-settings-client-cache.h \
	e-settings-comp-editor.c \
	e-settings-comp-editor.h \
	e-settings-date-edit.c \
	e-settings-date-edit.h \
	e-settings-deprecated.c \
	e-settings-deprecated.h \
	e-settings-mail-browser.c \
	e-settings-mail-browser.h \
	e-settings-mail-formatter.c \
	e-settings-mail-formatter.h \
	e-settings-mail-part-headers.c \
	e-settings-mail-part-headers.h \
	e-settings-mail-reader.c \
	e-settings-mail-reader.h \
	e-settings-mail-session.c \
	e-settings-mail-session.h \
	e-settings-meeting-store.c \
	e-settings-meeting-store.h \
	e-settings-meeting-time-selector.c \
	e-settings-meeting-time-selector.h \
	e-settings-message-list.c \
	e-settings-message-list.h \
	e-settings-name-selector-entry.c \
	e-settings-name-selector-entry.h \
	e-settings-spell-entry.c \
	e-settings-spell-entry.h \
	e-settings-web-view.c \
	e-settings-web-view.h \
	e-settings-web-view-gtkhtml.c \
	e-settings-web-view-gtkhtml.h \
	e-settings-weekday-chooser.c \
	e-settings-weekday-chooser.h \
	$(NULL)

module_settings_la_LIBADD = \
	$(top_builddir)/libemail-engine/libemail-engine.la \
	$(top_builddir)/e-util/libevolution-util.la \
	$(top_builddir)/em-format/libevolution-mail-formatter.la \
	$(top_builddir)/shell/libevolution-shell.la \
	$(top_builddir)/mail/libevolution-mail.la \
	$(top_builddir)/calendar/gui/libevolution-calendar.la \
	$(EVOLUTION_DATA_SERVER_LIBS) \
	$(GNOME_PLATFORM_LIBS) \
	$(GTKHTML_LIBS) \
	$(NULL)

module_settings_la_LDFLAGS = \
	-avoid-version -module $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk
