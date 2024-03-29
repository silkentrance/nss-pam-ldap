/*
   common.h - common server code routines
   This file is part of the nss-pam-ldapd library.

   Copyright (C) 2006 West Consulting
   Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 Arthur de Jong

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

#ifndef NSLCD__COMMON_H
#define NSLCD__COMMON_H 1

#include <errno.h>

#include "nslcd.h"
#include "common/nslcd-prot.h"
#include "common/tio.h"
#include "compat/attrs.h"
#include "myldap.h"

/* macros for basic read and write operations, the following
   ERROR_OUT* marcos define the action taken on errors
   the stream is not closed because the caller closes the
   stream */

#define ERROR_OUT_WRITEERROR(fp) \
  log_log(LOG_WARNING,"error writing to client: %s",strerror(errno)); \
  return -1;

#define ERROR_OUT_READERROR(fp) \
  log_log(LOG_WARNING,"error reading from client: %s",strerror(errno)); \
  return -1;

#define ERROR_OUT_BUFERROR(fp) \
  log_log(LOG_WARNING,"client supplied argument %d bytes too large",tmpint32); \
  return -1;

/* a simple wrapper around snprintf,
   returns 0 if ok, -1 on error */
int mysnprintf(char *buffer,size_t buflen,const char *format, ...)
  LIKE_PRINTF(3,4);

/* return the fully qualified domain name of the current host
   the returned value does not need to be freed but is re-used for every
   call */
MUST_USE const char *getfqdn(void);

/* This tries to get the user password attribute from the entry.
   It will try to return an encrypted password as it is used in /etc/passwd,
   /etc/group or /etc/shadow depending upon what is in the directory.
   This function will return NULL if no passwd is found and will return the
   literal value in the directory if conversion is not possible. */
const char *get_userpassword(MYLDAP_ENTRY *entry,const char *attr,
                             char *buffer,size_t buflen);

/* write out an address, parsing the addr value */
int write_address(TFILE *fp,const char *addr);

/* a helper macro to write out addresses and bail out on errors */
#define WRITE_ADDRESS(fp,addr) \
  if (write_address(fp,addr)) \
    return -1;

/* read an address from the stream */
int read_address(TFILE *fp,char *addr,int *addrlen,int *af);

/* helper macro to read an address from the stream */
#define READ_ADDRESS(fp,addr,len,af) \
  len=(int)sizeof(addr); \
  if (read_address(fp,addr,&(len),&(af))) \
    return -1;

/* convert the provided string representation of a sid
   (e.g. S-1-5-21-1936905831-823966427-12391542-23578)
   to a format that can be used to search the objectSid property with */
MUST_USE char *sid2search(const char *sid);

/* return the last security identifier of the binary sid */
MUST_USE long int binsid2id(const char *binsid);

/* checks to see if the specified string is a valid user or group name */
MUST_USE int isvalidname(const char *name);

/* Perform an LDAP lookup to translate the DN into a uid.
   This function either returns NULL or a strdup()ed string. */
MUST_USE char *lookup_dn2uid(MYLDAP_SESSION *session,const char *dn,int *rcp,char *buf,size_t buflen);

/* transforms the DN info a uid doing an LDAP lookup if needed */
MUST_USE char *dn2uid(MYLDAP_SESSION *session,const char *dn,char *buf,size_t buflen);

/* use the user id to lookup an LDAP entry */
MYLDAP_ENTRY *uid2entry(MYLDAP_SESSION *session,const char *uid,int *rcp);

/* transforms the uid into a DN by doing an LDAP lookup */
MUST_USE char *uid2dn(MYLDAP_SESSION *session,const char *uid,char *buf,size_t buflen);

/* try to update the shadowLastChange attribute of the entry if possible */
int update_lastchange(MYLDAP_SESSION *session,const char *userdn);

/* use the user id to lookup an LDAP entry with the shadow attributes
   requested*/
MYLDAP_ENTRY *shadow_uid2entry(MYLDAP_SESSION *session,const char *username,int *rcp);

/* return shadown information */
void get_shadow_properties(MYLDAP_ENTRY *entry,long *lastchangedate,
                           long *mindays,long *maxdays,long *warndays,
                           long *inactdays,long *expiredate,unsigned long *flag);


/* check whether the nsswitch.conf file has LDAP as a naming source for db */
int nsswitch_db_uses_ldap(const char *filename,const char *db);

/* fallback definition of HOST_NAME_MAX */
#ifndef HOST_NAME_MAX
#ifdef _POSIX_HOST_NAME_MAX
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#else
#define HOST_NAME_MAX 255
#endif /* _POSIX_HOST_NAME_MAX */
#endif /* not HOST_NAME_MAX */

/* provide strtouid() function alias */
#if SIZEOF_UID_T == SIZEOF_UNSIGNED_LONG_INT
#define strtouid (uid_t)strtoul
#elif SIZEOF_UID_T == SIZEOF_UNSIGNED_LONG_LONG_INT
#define strtouid (uid_t)strtoull
#elif SIZEOF_UID_T == SIZEOF_UNSIGNED_INT
#define WANT_STRTOUI 1
#define strtouid (uid_t)strtoui
#else
#error unable to find implementation for strtouid()
#endif

/* provide strtouid() function alias */
#if SIZEOF_GID_T == SIZEOF_UNSIGNED_LONG_INT
#define strtogid (gid_t)strtoul
#elif SIZEOF_GID_T == SIZEOF_UNSIGNED_LONG_LONG_INT
#define strtogid (gid_t)strtoull
#elif SIZEOF_GID_T == SIZEOF_UNSIGNED_INT
#ifndef WANT_STRTOUI
#define WANT_STRTOUI 1
#endif
#define strtogid (uid_t)strtoui
#else
#error unable to find implementation for strtogid()
#endif

#ifdef WANT_STRTOUI
/* provide a strtoui() if it is needed */
unsigned int strtoui(const char *nptr,char **endptr,int base);
#endif /* WANT_STRTOUI */

/* these are the functions for initialising the database specific
   modules */
void alias_init(void);
void ether_init(void);
void group_init(void);
void host_init(void);
void netgroup_init(void);
void network_init(void);
void passwd_init(void);
void protocol_init(void);
void rpc_init(void);
void service_init(void);
void shadow_init(void);

/* these are the different functions that handle the database
   specific actions, see nslcd.h for the action descriptions */
int nslcd_alias_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_alias_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_ether_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_ether_byether(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_ether_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_group_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_group_bygid(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_group_bymember(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_group_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_host_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_host_byaddr(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_host_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_netgroup_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_network_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_network_byaddr(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_network_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_passwd_byname(TFILE *fp,MYLDAP_SESSION *session,uid_t calleruid);
int nslcd_passwd_byuid(TFILE *fp,MYLDAP_SESSION *session,uid_t calleruid);
int nslcd_passwd_all(TFILE *fp,MYLDAP_SESSION *session,uid_t calleruid);
int nslcd_protocol_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_protocol_bynumber(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_protocol_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_rpc_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_rpc_bynumber(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_rpc_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_service_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_service_bynumber(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_service_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_shadow_byname(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_shadow_all(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_pam_authc(TFILE *fp,MYLDAP_SESSION *session,uid_t calleruid);
int nslcd_pam_authz(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_pam_sess_o(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_pam_sess_c(TFILE *fp,MYLDAP_SESSION *session);
int nslcd_pam_pwmod(TFILE *fp,MYLDAP_SESSION *session,uid_t calleruid);

/* macros for generating service handling code */
#define NSLCD_HANDLE(db,fn,readfn,action,mkfilter,writefn) \
  int nslcd_##db##_##fn(TFILE *fp,MYLDAP_SESSION *session) \
  NSLCD_HANDLE_BODY(db,fn,readfn,action,mkfilter,writefn)
#define NSLCD_HANDLE_UID(db,fn,readfn,action,mkfilter,writefn) \
  int nslcd_##db##_##fn(TFILE *fp,MYLDAP_SESSION *session,uid_t calleruid) \
  NSLCD_HANDLE_BODY(db,fn,readfn,action,mkfilter,writefn)
#define NSLCD_HANDLE_BODY(db,fn,readfn,action,mkfilter,writefn) \
  { \
    /* define common variables */ \
    int32_t tmpint32; \
    MYLDAP_SEARCH *search; \
    MYLDAP_ENTRY *entry; \
    const char *base; \
    int rc,i; \
    /* read request parameters */ \
    readfn; \
    /* write the response header */ \
    WRITE_INT32(fp,NSLCD_VERSION); \
    WRITE_INT32(fp,action); \
    /* prepare the search filter */ \
    if (mkfilter) \
    { \
      log_log(LOG_WARNING,"nslcd_" __STRING(db) "_" __STRING(fn) "(): filter buffer too small"); \
      return -1; \
    } \
    /* perform a search for each search base */ \
    for (i=0; (base=db##_bases[i])!=NULL; i++) \
    { \
      /* do the LDAP search */ \
      if ((search=myldap_search(session,base,db##_scope,filter,db##_attrs,NULL))==NULL) \
        return -1; \
      /* go over results */ \
      while ((entry=myldap_get_entry(search,&rc))!=NULL) \
      { \
        if (writefn) \
          return -1; \
      } \
    } \
    /* write the final result code */ \
    if (rc==LDAP_SUCCESS) \
    { \
      WRITE_INT32(fp,NSLCD_RESULT_END); \
    } \
    return 0; \
  }

#endif /* not NSLCD__COMMON_H */
