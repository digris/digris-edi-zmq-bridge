# Copyright (C) 2025 Matthias P. Braendli, http://opendigitalradio.org
AC_PREREQ([2.69])
AC_INIT([DIGRIS-EDI-ZMQ-BRIDGE],[0.9.0],[matthias.braendli@mpb.li])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AC_CONFIG_SRCDIR([src/edi2edi.cpp])
AC_CONFIG_HEADERS([config.h])
AM_SILENT_RULES([yes])

# Checks for programs.
AM_PROG_AR
AC_PROG_CXX
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_RANLIB
AC_PROG_INSTALL

AX_CXX_COMPILE_STDCXX(17,noext,mandatory)

# Checks for libraries.
AX_PTHREAD([], AC_MSG_ERROR([requires pthread]))

AX_ZMQ([4.0.0], [], AC_MSG_ERROR(ZeroMQ 4.0.0 is required))

# Checks for header files.
AC_PROG_EGREP

AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])

AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Wduplicated-cond], [CXXFLAGS="$CXXFLAGS -Wduplicated-cond"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wduplicated-branches], [CXXFLAGS="$CXXFLAGS -Wduplicated-branches"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wlogical-op], [CXXFLAGS="$CXXFLAGS -Wlogical-op"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wrestrict], [CXXFLAGS="$CXXFLAGS -Wrestrict"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wdouble-promotion], [CXXFLAGS="$CXXFLAGS -Wdouble-promotion"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG(["-Wformat=2"], [CXXFLAGS="$CXXFLAGS -Wformat=2"], [], ["-Werror"])

# Linux defines MSG_NOSIGNAL, some other systems have SO_NOSIGPIPE instead
AC_MSG_CHECKING(for MSG_NOSIGNAL)
AC_COMPILE_IFELSE([ AC_LANG_PROGRAM([[
                    #include <sys/socket.h>
                    int f = MSG_NOSIGNAL;
                    ]])],
                   [ AC_MSG_RESULT(yes)
                     AC_DEFINE(HAVE_MSG_NOSIGNAL, 1, [Define this symbol if you have MSG_NOSIGNAL]) ],
                   [ AC_MSG_RESULT(no) ])

AC_MSG_CHECKING(for SO_NOSIGPIPE)
AC_COMPILE_IFELSE([ AC_LANG_PROGRAM([[
                    #include <sys/socket.h>
                    int f = SO_NOSIGPIPE;
                    ]])],
                   [ AC_MSG_RESULT(yes)
                     AC_DEFINE(HAVE_SO_NOSIGPIPE, 1, [Define this symbol if you have SO_NOSIGPIPE]) ],
                   [ AC_MSG_RESULT(no) ])

# On some platforms, std::atomic needs a helper library
AC_MSG_CHECKING(whether -latomic is needed)
AC_LINK_IFELSE([AC_LANG_SOURCE([[
  #include <atomic>
  #include <cstdint>
  std::atomic<std::int64_t> v;
  int main() {
    return v;
  }
]])], STD_ATOMIC_NEED_LIBATOMIC=no, STD_ATOMIC_NEED_LIBATOMIC=yes)
AC_MSG_RESULT($STD_ATOMIC_NEED_LIBATOMIC)
if test "x$STD_ATOMIC_NEED_LIBATOMIC" = xyes; then
  LIBATOMIC_LIBS="-latomic"
fi
AC_SUBST([LIBATOMIC_LIBS])

AC_LANG_POP([C++])


AM_CONDITIONAL([IS_GIT_REPO], [test -d '.git'])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT



echo "***********************************************"
echo "Configured, please run make"
echo
