/*
 * switchs.h
 *
 *  Created on: May 25, 2018
 *      Author: pi
 */

#ifndef SWITCHS_H_
#define SWITCHS_H_

// *****
// Shamelessly stolen from https://gist.github.com/HoX/abfe15c40f2d9daebc35
// Modified to remove regex
// *****

#include <string.h>
#include <stdbool.h>

/** Begin a switch for the string x */
#define switchs(x) \
    { char *__sw = (x); bool __done = false; bool __cont = false; \
        do {

/** Check if the string matches the cases argument (case sensitive) */
#define cases(x)    } if ( __cont || !strcmp ( __sw, x ) ) \
                        { __done = true; __cont = true;

/** Check if the string matches the icases argument (case insensitive) */
#define icases(x)    } if ( __cont || !strcasecmp ( __sw, x ) ) { \
                        __done = true; __cont = true;

/** Default behaviour */
#define defaults    } if ( !__done || __cont ) {

/** Close the switchs */
#define switchs_end } while ( 0 ); }

#endif /* SWITCHS_H_ */

/* #include <stdio.h>
#include "switchs.h"

int main(int argc, char **argv) {
     switchs(argv[1]) {
        cases("foo")
        cases("bar")
            printf("foo or bar (case sensitive)\n");
            break;

        icases("pi")
            printf("pi or Pi or pI or PI (case insensitive)\n");
            break;

        cases_re("^D.*",0)
            printf("Something that start with D (case sensitive)\n");
            break;

        cases_re("^E.*",REG_ICASE)
            printf("Something that start with E (case insensitive)\n");
            break;

        cases("1")
            printf("1\n");

        cases("2")
            printf("2\n");
            break;

        defaults
            printf("No match\n");
            break;
    } switchs_end;

    return 0;
}
*/
