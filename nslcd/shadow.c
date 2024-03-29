/*
   shadow.c - shadow entry lookup routines
   Parts of this file were part of the nss_ldap library (as ldap-spwd.c)
   which has been forked into the nss-pam-ldapd library.

   Copyright (C) 1997-2005 Luke Howard
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "log.h"
#include "myldap.h"
#include "cfg.h"
#include "attmap.h"

/* ( nisSchema.2.1 NAME 'shadowAccount' SUP top AUXILIARY
 *   DESC 'Additional attributes for shadow passwords'
 *   MUST uid
 *   MAY ( userPassword $ shadowLastChange $ shadowMin
 *         shadowMax $ shadowWarning $ shadowInactive $
 *         shadowExpire $ shadowFlag $ description ) )
 */

/* the search base for searches */
const char *shadow_bases[NSS_LDAP_CONFIG_MAX_BASES] = { NULL };

/* the search scope for searches */
int shadow_scope = LDAP_SCOPE_DEFAULT;

/* the basic search filter for searches */
const char *shadow_filter = "(objectClass=shadowAccount)";

/* the attributes to request with searches */
const char *attmap_shadow_uid              = "uid";
const char *attmap_shadow_userPassword     = "\"*\"";
const char *attmap_shadow_shadowLastChange = "\"${shadowLastChange:--1}\"";
const char *attmap_shadow_shadowMin        = "\"${shadowMin:--1}\"";
const char *attmap_shadow_shadowMax        = "\"${shadowMax:--1}\"";
const char *attmap_shadow_shadowWarning    = "\"${shadowWarning:--1}\"";
const char *attmap_shadow_shadowInactive   = "\"${shadowInactive:--1}\"";
const char *attmap_shadow_shadowExpire     = "\"${shadowExpire:--1}\"";
const char *attmap_shadow_shadowFlag       = "\"${shadowFlag:-0}\"";

/* default values for attributes */
static const char *default_shadow_userPassword     = "*"; /* unmatchable */

/* the attribute list to request with searches */
static const char **shadow_attrs=NULL;

static int mkfilter_shadow_byname(const char *name,
                                  char *buffer,size_t buflen)
{
  char safename[300];
  /* escape attribute */
  if(myldap_escape(name,safename,sizeof(safename)))
    return -1;
  /* build filter */
  return mysnprintf(buffer,buflen,
                    "(&%s(%s=%s))",
                    shadow_filter,
                    attmap_shadow_uid,safename);
}

void shadow_init(void)
{
  int i;
  SET *set;
  /* set up search bases */
  if (shadow_bases[0]==NULL)
    for (i=0;i<NSS_LDAP_CONFIG_MAX_BASES;i++)
      shadow_bases[i]=nslcd_cfg->ldc_bases[i];
  /* set up scope */
  if (shadow_scope==LDAP_SCOPE_DEFAULT)
    shadow_scope=nslcd_cfg->ldc_scope;
  /* set up attribute list */
  set=set_new();
  attmap_add_attributes(set,attmap_shadow_uid);
  attmap_add_attributes(set,attmap_shadow_userPassword);
  attmap_add_attributes(set,attmap_shadow_shadowLastChange);
  attmap_add_attributes(set,attmap_shadow_shadowMax);
  attmap_add_attributes(set,attmap_shadow_shadowMin);
  attmap_add_attributes(set,attmap_shadow_shadowWarning);
  attmap_add_attributes(set,attmap_shadow_shadowInactive);
  attmap_add_attributes(set,attmap_shadow_shadowExpire);
  attmap_add_attributes(set,attmap_shadow_shadowFlag);
  shadow_attrs=set_tolist(set);
  set_free(set);
}

static long to_date(const char *date,const char *attr)
{
  char buffer[32];
  long value;
  char *tmp;
  size_t l;
  /* do some special handling for date values on AD */
  if (strcasecmp(attr,"pwdLastSet")==0)
  {
    /* we expect an AD 64-bit datetime value;
       we should do date=date/864000000000-134774
       but that causes problems on 32-bit platforms,
       first we devide by 1000000000 by stripping the
       last 9 digits from the string and going from there */
    l=strlen(date)-9;
    if (l>(sizeof(buffer)-1))
      return -1; /* error */
    strncpy(buffer,date,l);
    buffer[l]='\0';
    errno=0;
    value=strtol(date,&tmp,0);
    if ((*date=='\0')||(*tmp!='\0'))
    {
      log_log(LOG_WARNING,"shadow entry contains non-numeric %s value",attr);
      return -1;
    }
    else if (errno!=0)
    {
      log_log(LOG_WARNING,"shadow entry contains too large %s value",attr);
      return -1;
    }
    return value/864-134774;
    /* note that AD does not have expiry dates but a lastchangeddate
       and some value that needs to be added */
  }
  errno=0;
  value=strtol(date,&tmp,0);
  if ((*date=='\0')||(*tmp!='\0'))
  {
    log_log(LOG_WARNING,"shadow entry contains non-numeric %s value",attr);
    return -1;
  }
  else if (errno!=0)
  {
    log_log(LOG_WARNING,"shadow entry contains too large %s value",attr);
    return -1;
  }
  return value;
}

#ifndef UF_DONT_EXPIRE_PASSWD
#define UF_DONT_EXPIRE_PASSWD 0x10000
#endif

#define GET_OPTIONAL_LONG(var,att,fallback) \
  tmpvalue=attmap_get_value(entry,attmap_shadow_##att,buffer,sizeof(buffer)); \
  if (tmpvalue==NULL) \
    tmpvalue=""; \
  errno=0; \
  var=strtol(tmpvalue,&tmp,0); \
  if ((*(tmpvalue)=='\0')||(*tmp!='\0')) \
  { \
    log_log(LOG_WARNING,"shadow entry %s contains non-numeric %s value", \
                        myldap_get_dn(entry),attmap_shadow_##att); \
    var=fallback; \
  } \
  else if (errno!=0) \
  { \
    log_log(LOG_WARNING,"shadow entry %s contains too large %s value", \
                        myldap_get_dn(entry),attmap_shadow_##att); \
    var=fallback; \
  }

void get_shadow_properties(MYLDAP_ENTRY *entry,long *lastchangedate,
                           long *mindays,long *maxdays,long *warndays,
                           long *inactdays,long *expiredate,unsigned long *flag)
{
  char buffer[64];
  const char *tmpvalue;
  char *tmp;
  /* get lastchange date */
  tmpvalue=attmap_get_value(entry,attmap_shadow_shadowLastChange,buffer,sizeof(buffer));
  if (tmpvalue==NULL)
    tmpvalue="";
  *lastchangedate=to_date(tmpvalue,attmap_shadow_shadowLastChange);
  /* get other shadow properties */
  GET_OPTIONAL_LONG(*mindays,shadowMin,-1);
  GET_OPTIONAL_LONG(*maxdays,shadowMax,-1);
  GET_OPTIONAL_LONG(*warndays,shadowWarning,-1);
  GET_OPTIONAL_LONG(*inactdays,shadowInactive,-1);
  GET_OPTIONAL_LONG(*expiredate,shadowExpire,-1);
  GET_OPTIONAL_LONG(*flag,shadowFlag,0);
  /* if we're using AD handle the flag specially */
  if (strcasecmp(attmap_shadow_shadowLastChange,"pwdLastSet")==0)
  {
    if (*flag&UF_DONT_EXPIRE_PASSWD)
      *maxdays=-1;
    *flag=0;
  }
}

/* try to update the shadowLastChange attribute of the entry if possible */
int update_lastchange(MYLDAP_SESSION *session,const char *userdn)
{
  MYLDAP_SEARCH *search;
  MYLDAP_ENTRY *entry;
  static const char *attrs[3];
  const char *attr;
  int rc;
  const char **values;
  LDAPMod mod,*mods[2];
  char buffer[64],*strvals[2];
  /* find the name of the attribute to use */
  if ( (attmap_shadow_shadowLastChange==NULL) || (attmap_shadow_shadowLastChange[0]=='\0') )
    return LDAP_LOCAL_ERROR; /* attribute not mapped at all */
  else if (strcmp(attmap_shadow_shadowLastChange,"\"${shadowLastChange:--1}\"")==0)
    attr="shadowLastChange";
  else if (attmap_shadow_shadowLastChange[0]=='\"')
    return LDAP_LOCAL_ERROR; /* other expressions not supported for now */
  else
    attr=attmap_shadow_shadowLastChange;
  /* set up the attributes we need */
  attrs[0]=attmap_shadow_uid;
  attrs[1]=attr;
  attrs[2]=NULL;
  /* find the entry to see if the attribute is present */
  search=myldap_search(session,userdn,LDAP_SCOPE_BASE,shadow_filter,attrs,&rc);
  if (search==NULL)
    return rc;
  entry=myldap_get_entry(search,&rc);
  if (entry==NULL)
    return rc;
  values=myldap_get_values(entry,attr);
  if ((values==NULL)||(values[0]==NULL)||(values[0][0]=='\0'))
    return LDAP_NO_SUCH_ATTRIBUTE;
  /* build the value for the new attribute */
  if (strcasecmp(attr,"pwdLastSet")==0)
  {
    /* for AD we use another timestamp */
    if(mysnprintf(buffer,sizeof(buffer),"%ld000000000",((long int)time(NULL)/100L+(134774L*864L))))
      return LDAP_LOCAL_ERROR;
  }
  else
  {
    /* time in days since Jan 1, 1970 */
    if(mysnprintf(buffer,sizeof(buffer),"%ld",((long int)(time(NULL)/(long int)(60*60*24)))))
      return LDAP_LOCAL_ERROR;
  }
  /* update the shadowLastChange attribute */
  strvals[0]=buffer;
  strvals[1]=NULL;
  mod.mod_op=LDAP_MOD_REPLACE;
  mod.mod_type=(char *)attr;
  mod.mod_values=strvals;
  mods[0]=&mod;
  mods[1]=NULL;
  rc=myldap_modify(session,userdn,mods);
  if (rc!=LDAP_SUCCESS)
    log_log(LOG_WARNING,"modification of %s attribute of %s failed: %s",
                        attr,userdn,ldap_err2string(rc));
  else
    log_log(LOG_DEBUG,"modification of %s attribute of %s succeeded",
                     attr,userdn);
  return rc;
}

static int write_shadow(TFILE *fp,MYLDAP_ENTRY *entry,const char *requser)
{
  int32_t tmpint32;
  const char **usernames;
  const char *passwd;
  long lastchangedate;
  long mindays;
  long maxdays;
  long warndays;
  long inactdays;
  long expiredate;
  unsigned long flag;
  int i;
  char passbuffer[64];
  /* get username */
  usernames=myldap_get_values(entry,attmap_shadow_uid);
  if ((usernames==NULL)||(usernames[0]==NULL))
  {
    log_log(LOG_WARNING,"shadow entry %s does not contain %s value",
                        myldap_get_dn(entry),attmap_shadow_uid);
    return 0;
  }
  /* get password */
  passwd=get_userpassword(entry,attmap_shadow_userPassword,passbuffer,sizeof(passbuffer));
  if (passwd==NULL)
    passwd=default_shadow_userPassword;
  /* get expiry properties */
  get_shadow_properties(entry,&lastchangedate,&mindays,&maxdays,&warndays,
                        &inactdays,&expiredate,&flag);
  /* write the entries */
  for (i=0;usernames[i]!=NULL;i++)
    if ((requser==NULL)||(strcmp(requser,usernames[i])==0))
    {
      WRITE_INT32(fp,NSLCD_RESULT_BEGIN);
      WRITE_STRING(fp,usernames[i]);
      WRITE_STRING(fp,passwd);
      WRITE_INT32(fp,lastchangedate);
      WRITE_INT32(fp,mindays);
      WRITE_INT32(fp,maxdays);
      WRITE_INT32(fp,warndays);
      WRITE_INT32(fp,inactdays);
      WRITE_INT32(fp,expiredate);
      WRITE_INT32(fp,flag);
    }
  return 0;
}

MYLDAP_ENTRY *shadow_uid2entry(MYLDAP_SESSION *session,const char *username,int *rcp)
{
  MYLDAP_SEARCH *search=NULL;
  MYLDAP_ENTRY *entry=NULL;
  const char *base;
  char filter[1024];
  int i;
  /* if it isn't a valid username, just bail out now */
  if (!isvalidname(username))
  {
    if (rcp!=NULL)
      *rcp=LDAP_INVALID_SYNTAX;
    return NULL;
  }
  /* we have to look up the entry */
  mkfilter_shadow_byname(username,filter,sizeof(filter));
  for (i=0;(i<NSS_LDAP_CONFIG_MAX_BASES)&&((base=shadow_bases[i])!=NULL);i++)
  {
    search=myldap_search(session,base,shadow_scope,filter,shadow_attrs,rcp);
    if (search==NULL)
    {
      if ((rcp!=NULL)&&(*rcp==LDAP_SUCCESS))
        *rcp=LDAP_NO_SUCH_OBJECT;
      return NULL;
    }
    entry=myldap_get_entry(search,rcp);
    if (entry!=NULL)
      return entry;
  }
  if ((rcp!=NULL)&&(*rcp==LDAP_SUCCESS))
    *rcp=LDAP_NO_SUCH_OBJECT;
  return NULL;
}

NSLCD_HANDLE(
  shadow,byname,
  char name[256];
  char filter[1024];
  READ_STRING(fp,name);
  log_setrequest("shadow=\"%s\"",name);,
  NSLCD_ACTION_SHADOW_BYNAME,
  mkfilter_shadow_byname(name,filter,sizeof(filter)),
  write_shadow(fp,entry,name)
)

NSLCD_HANDLE(
  shadow,all,
  const char *filter;
  log_setrequest("shadow(all)");,
  NSLCD_ACTION_SHADOW_ALL,
  (filter=shadow_filter,0),
  write_shadow(fp,entry,NULL)
)
