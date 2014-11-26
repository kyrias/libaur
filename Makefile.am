ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

bin_PROGRAMS =
noinst_LTLIBRARIES =

if USE_GIT_VERSION
GIT_VERSION := $(shell git describe --abbrev=4 --dirty | sed 's/^v//')
REAL_PACKAGE_VERSION = $(GIT_VERSION)
else
REAL_PACKAGE_VERSION = $(PACKAGE_VERSION)
endif

AM_CFLAGS = $(WARNINGFLAGS)
# AM_LDFLAGS = $(GCLDFLAGS)

AM_CPPFLAGS = \
	-imacros $(top_builddir)/config.h \
	-I $(top_srcdir)/src

if USE_GIT_VERSION
AM_CPPFLAGS += \
	-DGIT_VERSION=\"$(GIT_VERSION)\"
endif

noinst_LTLIBRARIES += \
	libaur.la

libaur_la_SOURCES = \
	src/aur-internal.h \
	src/aur.c \
	src/aur.h \
	src/macro.h \
	src/package.c \
	src/request.c

libaur_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(CURL_CFLAGS) \
	$(YAJL_CFLAGS)

libaur_la_LIBADD = \
	$(CURL_LIBS) \
	$(YAJL_LIBS)

bin_PROGRAMS += \
	cow

cow_SOURCES = \
	src/cow.c

cow_CFLAGS = \
	$(AM_CFLAGS) \
	$(ARCHIVE_CFLAGS)

cow_LDADD = \
	$(ARCHIVE_LIBS) \
	libaur.la
