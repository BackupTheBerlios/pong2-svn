dnl Process this file with autoconf to produce a configure script.
AC_INIT(src/main.cpp)
AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE(pong2, 0.1.0)

AM_CONFIG_HEADER(config.h)

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX

AC_PROG_RANLIB

dnl Checks for header files.
AC_HEADER_STDC

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Check for headers
# AC_CHECK_HEADERS([jpeglib.h])

dnl Checks for libraries.
AC_CHECK_LIB([GL], [glBegin],, AC_MSG_ERROR([Error! You do not have OpenGL
installed. You will not be able to play this game.]))

AC_CHECK_LIB([GLU], [gluLookAt],, AC_MSG_ERROR([Error! You do not have OpenGL
installed. You will not be able to play this game.]))

# AC_CHECK_LIB([jpeg], [jpeg_start_compress],, AC_MSG_ERROR([libjpeg not found!]))

AC_CHECK_LIB([SDL_net], [SDLNet_Init],, AC_MSG_ERROR([Error! You need
SDL_net. Check out http://www.libsdl.org/projects/SDL_net/ for
more info.]))

# AC_CHECK_LIB([SDL_ttf], [TTF_OpenFont],, AC_MSG_ERROR([Error! You need SDL_ttf.
# Check out http://www.libsdl.org/projects/SDL_ttf/ for more info.]))

# AC_CHECK_LIB([SDL_image], [IMG_Load],, AC_MSG_ERROR([SDL_image not found]))

dnl Check for SDL
SDL_VERSION=1.2.4
AM_PATH_SDL($SDL_VERSION,
           :,
           AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!])
                        )
dnl CFLAGS="$CFLAGS $SDL_CFLAGS"
CFLAGS="$CFLAGS $SDL_CFLAGS -DPATH_PREFIX=\\\"\$(datadir)/\$(PACKAGE)/\\\" -O3"
CXXFLAGS="$CFLAGS"
LIBS="$LIBS $SDL_LIBS"

dnl Checks for library functions.

AC_OUTPUT([Makefile src/Makefile src/net/Makefile data/Makefile])
