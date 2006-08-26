dnl Process this file with autoconf to produce a configure script.
AC_INIT(src/main.cpp)
AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE(pong2, 0.1.2)

AM_CONFIG_HEADER(config.h)

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX

AC_PROG_RANLIB

dnl Checks for header files.
AC_HEADER_STDC

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Check for pthreads
ACX_PTHREAD(have_pthreads=yes, have_pthreads=no)

dnl Check for SSL
AC_CHECK_HEADER([openssl/ssl.h], CFLAGS="$CFLAGS -DSOCK_SSL" LIBS="$LIBS -lssl",)

if [[ "x$have_pthreads" = "xyes" ]]; then
  LIBS="$PTHREAD_LIBS $LIBS"
  CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
  CC="$PTHREAD_CC"
else
  AC_MSG_ERROR([cannot find pthreads])
fi

dnl Checks for libraries.
AC_CHECK_LIB([GL], [glBegin],, AC_MSG_ERROR([Error! You do not have OpenGL
installed. You will not be able to play this game.]))

AC_CHECK_LIB([GLU], [gluLookAt],, AC_MSG_ERROR([Error! You do not have OpenGL
installed. You will not be able to play this game.]))

# AC_CHECK_LIB([SDL_net], [SDLNet_Init],, AC_MSG_ERROR([Error! You need
# SDL_net. Check out http://www.libsdl.org/projects/SDL_net/ for
# more info.]))

# AC_CHECK_LIB([SDL_ttf], [TTF_OpenFont],, AC_MSG_ERROR([Error! You need SDL_ttf.
# Check out http://www.libsdl.org/projects/SDL_ttf/ for more info.]))

AC_CHECK_LIB([SDL_image], [IMG_Load],, AC_MSG_ERROR([SDL_image not found!]))

dnl Check for SDL
SDL_VERSION=1.2.4
AM_PATH_SDL($SDL_VERSION,:,
			AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!]))
dnl CFLAGS="$CFLAGS $SDL_CFLAGS"
CXXFLAGS="$CFLAGS $SDL_CFLAGS -DPATH_PREFIX=\\\"\$(datadir)/\$(PACKAGE)/\\\""
LIBS="$LIBS $SDL_LIBS"

dnl Checks for library functions.

AC_OUTPUT([Makefile src/Makefile src/grapple/Makefile data/Makefile])
