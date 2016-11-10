
AC_PREREQ(2.64)

AC_INIT(libaur,
        [0],
        [d@falconindy.com],
        [libaur],
        [/dev/null])

AC_CONFIG_SRCDIR([src/aur.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AC_PREFIX_DEFAULT([/usr])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CANONICAL_HOST
AC_PROG_CC_C99
AC_PROG_INSTALL
AC_PROG_SED
AC_PROG_MKDIR_P

AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects])
AM_SILENT_RULES([yes])

LT_PREREQ(2.2)
LT_INIT

PKG_CHECK_MODULES(CURL,    [ libcurl >= 7.28.0 ])
PKG_CHECK_MODULES(YAJL,    [ yajl >= 2.0.0 ])
PKG_CHECK_MODULES(LIBGIT2, [ libgit2 >= 0.22.0 ])

# Help line for using git version in pkgfile version string
AC_ARG_ENABLE(git-version,
	AS_HELP_STRING([--disable-git-version],
		[enable use of git version in version string if available]),
	[wantgitver=$enableval], [wantgitver=check])

# Enable or disable use of git version in pkgfile version string
AC_MSG_CHECKING(whether to use git version if available)
if test "$wantgitver" != "no" ; then
	AC_CHECK_PROGS([GIT], [git])
	AC_CHECK_FILE([.git/], hasgitdir=yes)
	if test "$GIT" && test "$hasgitdir" = "yes"; then
		usegitver=yes
		AC_DEFINE([USE_GIT_VERSION], , [Use GIT version in version string])
	else
		usegitver=no
	fi
else
	AC_MSG_RESULT([no, disabled by configure])
	usegitver=no
fi
AM_CONDITIONAL(USE_GIT_VERSION, test "$usegitver" = "yes")

AC_ARG_WITH(aur-domain,
        AS_HELP_STRING([--with-aur-domain=DOMAIN],
                [Domain of the AUR]),
        [AUR_DOMAIN="$withval"],
        [AUR_DOMAIN="aur.archlinux.org"])
AC_DEFINE_UNQUOTED(AUR_DOMAIN, ["$AUR_DOMAIN"], [Domain of the AUR])

CC_CHECK_FLAGS_APPEND(with_cflags, [CFLAGS], [\
	-g \
	-pipe \
	-Wall \
	-W \
	-Wextra \
	-Wno-inline \
	-Wvla \
	-Wundef \
	"-Wformat=2 -Wformat-security -Wno-format-nonliteral" \
	-Wformat=2 \
	-Wlogical-op \
	-Wsign-compare \
	-Wmissing-include-dirs \
	-Wold-style-definition \
	-Wpointer-arith \
	-Winit-self \
	-Wfloat-equal \
	-Wmissing-prototypes \
	-Wstrict-prototypes \
	-Wredundant-decls \
	-Wmissing-declarations \
	-Wmissing-noreturn \
	-Wshadow \
	-Wendif-labels \
	-Wstrict-aliasing=2 \
	-Wwrite-strings \
	-Wno-long-long \
	-Wno-overlength-strings \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-Wno-unused-result \
	-Wnested-externs \
	-Wchar-subscripts \
	-Wtype-limits \
	-Wuninitialized])
AC_SUBST([WARNINGFLAGS], $with_cflags)

CC_CHECK_FLAGS_APPEND([with_ldflags], [LDFLAGS], [ \
	-pie \
	-Wl,--as-needed])
AC_SUBST([GCLDFLAGS], $with_ldflags)

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([
	Makefile
])

AC_OUTPUT
AC_MSG_RESULT([
	prefix:                 $(eval echo ${prefix})
	datarootdir:            $(eval echo ${datarootdir})

	using git version:      ${usegitver}

	compiler:               ${CC}
	cflags:                 ${with_cflags} ${CFLAGS}
	ldflags:                ${with_ldflags} ${LDFLAGS}
])
