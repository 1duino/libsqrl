/** @file util.c -- Various utility functions 

@author Adam Comley

This file is part of libsqrl.  It is released under the MIT license.
For more details, see the LICENSE file included with this package.
**/

#include "sqrl_internal.h"
#include "version.h"
#include "sqrl.h"
#include "SqrlUser.h"
#include "SqrlAction.h"
#include "SqrlEntropy.h"
#include "gcm.h"


static bool sqrl_is_initialized = false;

struct Sqrl_Global_Mutices SQRL_GLOBAL_MUTICES;

/**
Initializes the SQRL library.  Must be called once, before any SQRL functions are used.
*/
DLL_PUBLIC
int sqrl_init()
{
	if( !sqrl_is_initialized ) {
		sqrl_is_initialized = true;
		SqrlEntropy::start();
		SQRL_GLOBAL_MUTICES.user = new std::mutex();
		SQRL_GLOBAL_MUTICES.site = new std::mutex();
		SQRL_GLOBAL_MUTICES.transaction = new std::mutex();
		#ifdef DEBUG
		DEBUG_PRINTF( "libsqrl %s\n", SQRL_LIB_VERSION );
		#endif
		gcm_initialize();
		return sodium_init();
	}
	return 0;
}

/**
Performs clean-up as a client is closing.  Erases and frees memory used by libsqrl.
If a User cannot safely be freed, it is hintlocked (encrypted).

\note Do not call any libsqrl functions after \p sqrl_stop()

@return Number of objects that could not safely be removed from memory, so they were encrypted.
@return -1 if libsqrl has not been initialized with \p sqrl_init()
*/
DLL_PUBLIC
int sqrl_stop()
{
	if( sqrl_is_initialized ) {
		SqrlEntropy::stop();
        int transactionCount, userCount, siteCount = 0;
#ifdef DEBUG
		transactionCount = SqrlAction::countTransactions();
		userCount = SqrlUser::countUsers();
		DEBUG_PRINTF( "%10s: %d open sites\n", "sqrl_stop", siteCount );
		DEBUG_PRINTF( "%10s: %d open transactions\n", "sqrl_stop", transactionCount );
		DEBUG_PRINTF( "%10s: %d open users\n", "sqrl_stop", userCount );
        DEBUG_PRINTF( "%10s: Cleaning Up...\n", "sqrl_stop" );
#endif
        //sqrl_client_site_maintenance( true );
        //sqrl_client_user_maintenance( true );
        transactionCount = SqrlAction::countTransactions();
		userCount = SqrlUser::countUsers();
        //siteCount = sqrl_site_count();
#ifdef DEBUG
        printf( "%10s: %d remain\n", "sqrl_stop", transactionCount + userCount + siteCount );
#endif
		return transactionCount + userCount + siteCount;
	}
	return -1;
}

void bin2rc( char *buf, uint8_t *bin ) 
{
	// bin must be 512+ bits of entropy!
	int i, j, k;
	uint64_t *tmp = (uint64_t*)bin;
	for( i = 0, j = 0; i < 3; i++ ) {
		for( k = 0; k < 8; k++ ) {
			buf[j++] = '0' + (tmp[k] % 10);
			tmp[k] /= 10;
		}
	}
	buf[j] = 0;
}

void sqrl_lcstr( char *str )
{
	int i;
	for( i = 0; str[i] != 0; i++ ) {
		if( str[i] > 64 && str[i] < 91 ) {
			str[i] += 32;
		}
	}
}

uint16_t readint_16( void *buf )
{
	uint8_t *b = (uint8_t*)buf;
	return (uint16_t)( b[0] | ( b[1] << 8 ));
}

bool sqrl_parse_key_value( char **strPtr, char **keyPtr, char **valPtr,
    size_t *key_len, size_t *val_len, char *sep )
{
    if( !*strPtr ) return false;
    char *p, *pp;
    p = strchr( *strPtr, '=' );
    if( p ) {
        *keyPtr = *strPtr;
        *key_len = p - *keyPtr;
        *valPtr = p + 1;
        pp = strstr( *valPtr, sep );
        if( pp ) {
            *val_len = pp - *valPtr;
            *strPtr = pp + strlen( sep );
        } else {
            *val_len = strlen( *valPtr );
            *strPtr = NULL;
        }
        return true;
    }
    *strPtr = NULL;
    return false;
}


size_t Sqrl_Version( char *buffer, size_t buffer_len ) {
	static const char *ver = SQRL_LIB_VERSION;
	size_t len = strlen( ver );
	strncpy( buffer, ver, buffer_len );
	return len;
}

uint16_t Sqrl_Version_Major() { return SQRL_LIB_VERSION_MAJOR; }
uint16_t Sqrl_Version_Minor() { return SQRL_LIB_VERSION_MINOR; }
uint16_t Sqrl_Version_Build() { return SQRL_LIB_VERSION_BUILD_DATE; }
uint16_t Sqrl_Version_Revision() { return SQRL_LIB_VERSION_REVISION; }
