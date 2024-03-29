/*
   pam_get_authtok.c - replacement function for pam_get_authtok()

   Copyright (C) 2009, 2010 Arthur de Jong
   Copyright (C) 2010 Symas Corporation

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>

#include "compat/attrs.h"
#include "compat/pam_compat.h"

/* warning: this version assumes that try_first_pass is specified */

/* find value of PAM_AUTHTOK_RECOVERY_ERR */
#ifndef PAM_AUTHTOK_RECOVERY_ERR
#ifdef PAM_AUTHTOK_RECOVER_ERR
#define PAM_AUTHTOK_RECOVERY_ERR PAM_AUTHTOK_RECOVER_ERR
#else
#define PAM_AUTHTOK_RECOVERY_ERR 21 /* not defined anywhere */
#endif
#endif

int pam_get_authtok(pam_handle_t *pamh,int item,const char **authtok,const char *prompt)
{
  int rc;
  char *passwd=NULL,*retype_passwd=NULL;
  const void *oldauthtok;
  char retype_prompt[80];
  /* first try to see if the value is already on the stack */
  *authtok=NULL;
  rc=pam_get_item(pamh,item,(const void **)authtok);
  if ((rc==PAM_SUCCESS)&&(*authtok!=NULL))
    return PAM_SUCCESS;
  /* check what to prompt for and provide default prompt */
  *retype_prompt='\0';
  if (item==PAM_OLDAUTHTOK)
    prompt=(prompt!=NULL)?prompt:"Old Password: ";
  else
  {
    rc=pam_get_item(pamh,PAM_OLDAUTHTOK,&oldauthtok);
    if ((rc==PAM_SUCCESS)&&(oldauthtok!=NULL))
    {
      prompt=(prompt!=NULL)?prompt:"New Password: ";
      snprintf(retype_prompt,sizeof(retype_prompt),"Retype %s",prompt);
      retype_prompt[sizeof(retype_prompt)-1]='\0';
    }
    else
      prompt=(prompt!=NULL)?prompt:"Password: ";
  }
  /* prepare prompt and get password */
  rc=pam_prompt(pamh,PAM_PROMPT_ECHO_OFF,&passwd,"%s",prompt);
  if (rc!=PAM_SUCCESS)
    return rc;
  /* if a second prompt should be presented, do it */
  if (*retype_prompt)
  {
    rc=pam_prompt(pamh,PAM_PROMPT_ECHO_OFF,&retype_passwd,"%s",retype_prompt);
    /* check passwords */
    if ((rc==PAM_SUCCESS)&&(strcmp(retype_passwd,passwd)!=0))
      rc=PAM_AUTHTOK_RECOVERY_ERR;
  }
  /* store the password if everything went ok */
  if (rc==PAM_SUCCESS)
    rc=pam_set_item(pamh,item,passwd);
  /* clear and free any password information */
  memset(passwd,0,strlen(passwd));
  free(passwd);
  if (retype_passwd!=NULL)
  {
    memset(retype_passwd,0,strlen(retype_passwd));
    free(retype_passwd);
  }
  if (rc!=PAM_SUCCESS)
    return rc;
  /* return token from the stack */
  return pam_get_item(pamh,item,(const void **)authtok);
}
