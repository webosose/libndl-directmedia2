/*
 * Copyright (c) 2008-2018 LG Electronics, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0



 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>

#include "OMX_Core.h"
#include "OMX_Types.h"
#include "OMX_ContentPipe.h"
#include "OMX_Component.h"


OMX_ERRORTYPE err;

//extern OMX_COMPONENTREGISTERTYPE OMX_ComponentRegistered[];

void listroles(char *name)
{
    int n;
    OMX_U32 numRoles;
    OMX_U8 *roles[32];

    /* get the number of roles by passing in a NULL roles param */
    err = OMX_GetRolesOfComponent(name, &numRoles, NULL);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Getting roles failed\n", 0);
        exit(1);
    }
    printf("  Num roles is %d\n", numRoles);
    if (numRoles > 32) {
        printf("Too many roles to list\n");
        return;
    }

    /* now get the roles */
    for (n = 0; n < numRoles; n++) {
        roles[n] = malloc(OMX_MAX_STRINGNAME_SIZE);
    }
    err = OMX_GetRolesOfComponent(name, &numRoles, roles);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Getting roles failed\n", 0);
        exit(1);
    }
    for (n = 0; n < numRoles; n++) {
        printf("    role: %s\n", roles[n]);
        free(roles[n]);
    }

    /* This is in version 1.2
       for (i = 0; OMX_ErrorNoMore != err; i++) {
       err = OMX_RoleOfComponentEnum(role, name, i);
       if (OMX_ErrorNone == err) {
       printf("   Role of omponent is %s\n", role);
       }
       }
       */
}

int main(int argc, char** argv)
{
    int i;
    unsigned char name[OMX_MAX_STRINGNAME_SIZE];

    err = OMX_Init();
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "OMX_Init() failed\n", 0);
        exit(1);
    }

    err = OMX_ErrorNone;
    for (i = 0; OMX_ErrorNoMore != err; i++) {
        err = OMX_ComponentNameEnum(name, OMX_MAX_STRINGNAME_SIZE, i);
        if (OMX_ErrorNone == err) {
            printf("Component is %s\n", name);
            listroles(name);
        }
    }
    printf("No more components\n");

    /*
       i= 0 ;
       while (1) {
       printf("Component %s\n", OMX_ComponentRegistered[i++]);
       }
       */
    exit(0);
}
