/*
   bsdnss.c - BSD NSS functions
   This file was part of the nss-pam-ldapd FreeBSD port and part of the
   nss_ldap FreeBSD port before that.

   Copyright (C) 2003 Jacques Vidrine
   Copyright (C) 2006 Artem Kazakov
   Copyright (C) 2009 Alexander V. Chernikov
   Copyright (C) 2011 Arthur de Jong

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA
*/

#include "config.h"

#include <errno.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <pwd.h>
#include <grp.h>
#include <nss.h>
#include <netdb.h>

#include "prototypes.h"

#define BUFFER_SIZE 1024

NSS_METHOD_PROTOTYPE(__nss_compat_getgrnam_r);
NSS_METHOD_PROTOTYPE(__nss_compat_getgrgid_r);
NSS_METHOD_PROTOTYPE(__nss_compat_getgrent_r);
NSS_METHOD_PROTOTYPE(__nss_compat_setgrent);
NSS_METHOD_PROTOTYPE(__nss_compat_endgrent);

NSS_METHOD_PROTOTYPE(__nss_compat_getpwnam_r);
NSS_METHOD_PROTOTYPE(__nss_compat_getpwuid_r);
NSS_METHOD_PROTOTYPE(__nss_compat_getpwent_r);
NSS_METHOD_PROTOTYPE(__nss_compat_setpwent);
NSS_METHOD_PROTOTYPE(__nss_compat_endpwent);

NSS_METHOD_PROTOTYPE(__nss_compat_gethostbyname);
NSS_METHOD_PROTOTYPE(__nss_compat_gethostbyname2);
NSS_METHOD_PROTOTYPE(__nss_compat_gethostbyaddr);

static ns_mtab methods[]={
  { NSDB_GROUP, "getgrnam_r", __nss_compat_getgrnam_r, _nss_ldap_getgrnam_r },
  { NSDB_GROUP, "getgrgid_r", __nss_compat_getgrgid_r, _nss_ldap_getgrgid_r },
  { NSDB_GROUP, "getgrent_r", __nss_compat_getgrent_r, _nss_ldap_getgrent_r },
  { NSDB_GROUP, "setgrent",   __nss_compat_setgrent,   _nss_ldap_setgrent },
  { NSDB_GROUP, "endgrent",   __nss_compat_endgrent,   _nss_ldap_endgrent },

  { NSDB_PASSWD, "getpwnam_r", __nss_compat_getpwnam_r, _nss_ldap_getpwnam_r },
  { NSDB_PASSWD, "getpwuid_r", __nss_compat_getpwuid_r, _nss_ldap_getpwuid_r },
  { NSDB_PASSWD, "getpwent_r", __nss_compat_getpwent_r, _nss_ldap_getpwent_r },
  { NSDB_PASSWD, "setpwent",   __nss_compat_setpwent,   _nss_ldap_setpwent },
  { NSDB_PASSWD, "endpwent",   __nss_compat_endpwent,   _nss_ldap_endpwent },

  { NSDB_HOSTS, "gethostbyname", __nss_compat_gethostbyname, _nss_ldap_gethostbyname_r },
  { NSDB_HOSTS, "gethostbyaddr", __nss_compat_gethostbyaddr, _nss_ldap_gethostbyaddr_r },
  { NSDB_HOSTS, "gethostbyname2", __nss_compat_gethostbyname2, _nss_ldap_gethostbyname2_r },

  { NSDB_GROUP_COMPAT, "getgrnam_r", __nss_compat_getgrnam_r, _nss_ldap_getgrnam_r },
  { NSDB_GROUP_COMPAT, "getgrgid_r", __nss_compat_getgrgid_r, _nss_ldap_getgrgid_r },
  { NSDB_GROUP_COMPAT, "getgrent_r", __nss_compat_getgrent_r, _nss_ldap_getgrent_r },
  { NSDB_GROUP_COMPAT, "setgrent",   __nss_compat_setgrent,   _nss_ldap_setgrent },
  { NSDB_GROUP_COMPAT, "endgrent",   __nss_compat_endgrent,   _nss_ldap_endgrent },

  { NSDB_PASSWD_COMPAT, "getpwnam_r", __nss_compat_getpwnam_r, _nss_ldap_getpwnam_r },
  { NSDB_PASSWD_COMPAT, "getpwuid_r", __nss_compat_getpwuid_r, _nss_ldap_getpwuid_r },
  { NSDB_PASSWD_COMPAT, "getpwent_r", __nss_compat_getpwent_r, _nss_ldap_getpwent_r },
  { NSDB_PASSWD_COMPAT, "setpwent",   __nss_compat_setpwent,   _nss_ldap_setpwent },
  { NSDB_PASSWD_COMPAT, "endpwent",   __nss_compat_endpwent,   _nss_ldap_endpwent },
};

int __nss_compat_gethostbyname(void *retval,void *mdata,va_list ap)
{
  nss_status_t (*fn)(const char *,struct hostent *,char *,size_t,int *,int *);
  const char *name;
  struct hostent *result;
  char buffer[BUFFER_SIZE];
  int errnop;
  int h_errnop;
  int af;
  nss_status_t status;
  fn=mdata;
  name=va_arg(ap,const char*);
  af=va_arg(ap,int);
  result=va_arg(ap,struct hostent *);
  status=fn(name,result,buffer,sizeof(buffer),&errnop,&h_errnop);
  status=__nss_compat_result(status,errnop);
  h_errno=h_errnop;
  return (status);
}

int __nss_compat_gethostbyname2(void *retval,void *mdata,va_list ap)
{
  nss_status_t (*fn)(const char *,struct hostent *,char *,size_t,int *,int *);
  const char *name;
  struct hostent *result;
  char buffer[BUFFER_SIZE];
  int errnop;
  int h_errnop;
  int af;
  nss_status_t status;
  fn=mdata;
  name=va_arg(ap,const char*);
  af=va_arg(ap,int);
  result=va_arg(ap,struct hostent *);
  status=fn(name,result,buffer,sizeof(buffer),&errnop,&h_errnop);
  status=__nss_compat_result(status,errnop);
  h_errno=h_errnop;
  return (status);
}

int __nss_compat_gethostbyaddr(void *retval,void *mdata,va_list ap)
{
  struct in_addr *addr;
  int len;
  int type;
  struct hostent *result;
  char buffer[BUFFER_SIZE];
  int errnop;
  int h_errnop;
  nss_status_t (*fn)(struct in_addr *,int,int,struct hostent *,char *,size_t,int *,int *);
  nss_status_t status;
  fn=mdata;
  addr=va_arg(ap,struct in_addr*);
  len=va_arg(ap,int);
  type=va_arg(ap,int);
  result=va_arg(ap,struct hostent*);
  status=fn(addr,len,type,result,buffer,sizeof(buffer),&errnop,&h_errnop);
  status=__nss_compat_result(status,errnop);
  h_errno=h_errnop;
  return (status);
}

ns_mtab *nss_module_register(const char *source,unsigned int *mtabsize,
    nss_module_unregister_fn *unreg)
{
  *mtabsize=sizeof(methods)/sizeof(methods[0]);
  *unreg=NULL;
  return (methods);
}
