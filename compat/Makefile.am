# Makefile.am - use automake to generate Makefile.in
#
# Copyright (C) 2008, 2009, 2010, 2011 Arthur de Jong
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301 USA

noinst_LIBRARIES = libcompat.a

AM_CPPFLAGS=-I$(top_srcdir)
AM_CFLAGS = -fPIC

EXTRA_DIST = getopt_long.c getopt_long.h \
             daemon.c daemon.h \
             ether.c ether.h \
             strndup.c strndup.h \
             nss_compat.h \
             ldap_compat.h pagectrl.c ldap_passwd_s.c ldap_initialize.c \
             pam_compat.h pam_get_authtok.c pam_prompt.c

libcompat_a_SOURCES = getpeercred.c getpeercred.h
libcompat_a_LIBADD = @LIBOBJS@
