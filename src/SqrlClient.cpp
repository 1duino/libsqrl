/** \file SqrlClient.cpp
 *
 * \author Adam Comley
 *
 * This file is part of libsqrl.  It is released under the MIT license.
 * For more details, see the LICENSE file included with this package.
**/

#include "sqrl_internal.h"

#include "SqrlClient.h"
#include "SqrlAction.h"
#include "SqrlUser.h"
#include "SqrlEntropy.h"
#include "gcm.h"

namespace libsqrl
{
    template class SqrlDeque<SqrlClient::CallbackInfo *>;

#define SQRL_CALLBACK_SAVE_SUGGESTED 0
#define SQRL_CALLBACK_SELECT_USER 1
#define SQRL_CALLBACK_SELECT_ALT 2
#define SQRL_CALLBACK_ACTION_COMPLETE 3
#define SQRL_CALLBACK_AUTH_REQUIRED 4
#define SQRL_CALLBACK_SEND 5
#define SQRL_CALLBACK_ASK 6
#define SQRL_CALLBACK_PROGRESS 7

    SqrlClient *SqrlClient::client = NULL;
#if defined(WITH_THREADS)
	std::mutex *SqrlClient::clientMutex = NULL;
#endif

    SqrlClient::SqrlClient() :
        rapid(false)
    {
#if defined(WITH_THREADS)
		if( SqrlClient::clientMutex == nullptr ) {
			SqrlClient::clientMutex = new std::mutex();
		}
#endif
		SQRL_MUTEX_LOCK( SqrlClient::clientMutex )
		if( SqrlClient::client != NULL ) {
			// Enforce a single SqrlClient object
			exit( 1 );
		}
		SqrlInit();

		SqrlEntropy::start();
		SqrlClient::client = this;
		SQRL_MUTEX_UNLOCK( SqrlClient::clientMutex )
	}

    SqrlClient::~SqrlClient() {
		this->onClientIsStopping();
        SQRL_MUTEX_LOCK( SqrlClient::clientMutex )
        SqrlClient::client = NULL;
		SQRL_MUTEX_UNLOCK( SqrlClient::clientMutex )

		SqrlEntropy::stop();

		SqrlAction *action;
		SqrlUser *user;
		do {
			SQRL_MUTEX_LOCK( &this->actionMutex );
			action = this->actions.pop();
			SQRL_MUTEX_UNLOCK( &this->actionMutex );
			if( action ) {
				delete action;
			}
		} while( action );
		do {
			SQRL_MUTEX_LOCK( &this->userMutex );
			user = this->users.pop();
			SQRL_MUTEX_UNLOCK( &this->userMutex );
			if( user ) {
				delete user;
			}
		} while( user );
    }

    SqrlClient *SqrlClient::getClient() {
		SqrlClient *client = nullptr;
		SQRL_MUTEX_LOCK( SqrlClient::clientMutex );
		client = SqrlClient::client;
		SQRL_MUTEX_UNLOCK( SqrlClient::clientMutex );
        return client;
    }

    int SqrlClient::getUserIdleSeconds() {
        return 0;
    }

    bool SqrlClient::isScreenLocked() {
        return false;
    }

    bool SqrlClient::isUserChanged() {
        return false;
    }

    void SqrlClient::onLoop() {
    }

    bool SqrlClient::loop() {
		if( SqrlClient::getClient() != this ) return false;
        this->onLoop();
        SqrlAction *action;
        while( !this->callbackQueue.empty() ) {
            struct CallbackInfo *info = this->callbackQueue.pop();

            switch( info->cbType ) {
            case SQRL_CALLBACK_SAVE_SUGGESTED:
                this->onSaveSuggested( (SqrlUser*)info->ptr );
                break;
            case SQRL_CALLBACK_SELECT_USER:
                action = (SqrlAction*)info->ptr;
                this->onSelectUser( action );
                break;
            case SQRL_CALLBACK_SELECT_ALT:
                action = (SqrlAction*)info->ptr;
                this->onSelectAlternateIdentity( action );
                break;
            case SQRL_CALLBACK_ACTION_COMPLETE:
                action = (SqrlAction*)info->ptr;
                this->onActionComplete( action );
                break;
            case SQRL_CALLBACK_AUTH_REQUIRED:
                action = (SqrlAction*)info->ptr;
                this->onAuthenticationRequired( action, info->credentialType );
                break;
            case SQRL_CALLBACK_SEND:
                action = (SqrlAction*)info->ptr;
                this->onSend( action, *info->str[0], *info->str[1] );
                break;
            case SQRL_CALLBACK_ASK:
                action = (SqrlAction*)info->ptr;
                this->onAsk( action, *info->str[0], *info->str[1], *info->str[2] );
                break;
            case SQRL_CALLBACK_PROGRESS:
                action = (SqrlAction*)info->ptr;
                this->onProgress( action, info->progress );
                break;
            }
            delete info;
        }
		SQRL_MUTEX_LOCK( &this->actionMutex );
		action = this->actions.pop();
		SQRL_MUTEX_UNLOCK( &this->actionMutex );
        if( action ) action->exec();

        if( this->actions.empty() && this->callbackQueue.empty() ) {
            return false;
        }
        return true;
    }

	SqrlUser * SqrlClient::getUser( const SqrlString * uniqueId ) {
		char testId[SQRL_UNIQUE_ID_LENGTH + 1];
		SQRL_MUTEX_LOCK( &this->userMutex );
		size_t end = this->users.count();
		for( size_t i = 0; i < end; i++ ) {
			SqrlUser *cur = this->users.peek( i );
			if( cur->getUniqueId( testId ) ) {
				if( 0 == uniqueId->compare( testId ) ) {
					SQRL_MUTEX_UNLOCK( &this->userMutex );
					return cur;
				}
			}
		}
		SQRL_MUTEX_UNLOCK( &this->userMutex );
		return nullptr;
	}

	SqrlUser * SqrlClient::getUser( void * tag ) {
		SQRL_MUTEX_LOCK( &this->userMutex );
		size_t end = this->users.count();
		for( size_t i = 0; i < end; i++ ) {
			SqrlUser *cur = this->users.peek( i );
			if( cur->getTag() == tag ) {
				SQRL_MUTEX_UNLOCK( &this->userMutex );
				return cur;
			}
		}
		SQRL_MUTEX_UNLOCK( &this->userMutex );
		return nullptr;
	}

    void SqrlClient::callSaveSuggested( SqrlUser * user ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_SAVE_SUGGESTED;
        info->ptr = user;
        this->callbackQueue.push( info );
    }

    void SqrlClient::callSelectUser( SqrlAction * action ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_SELECT_USER;
        info->ptr = action;
        this->callbackQueue.push( info );
    }

    void SqrlClient::callSelectAlternateIdentity( SqrlAction * action ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_SELECT_ALT;
        info->ptr = action;
        this->callbackQueue.push( info );
    }

    void SqrlClient::callActionComplete( SqrlAction * action ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_ACTION_COMPLETE;
        info->ptr = action;
        this->callbackQueue.push( info );
    }

    void SqrlClient::callProgress( SqrlAction * action, int progress ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_PROGRESS;
        info->ptr = action;
        info->progress = progress;
        this->callbackQueue.push_back( info );
    }

    void SqrlClient::callAuthenticationRequired( SqrlAction * action, Sqrl_Credential_Type credentialType ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_AUTH_REQUIRED;
        info->ptr = action;
        info->credentialType = credentialType;
        this->callbackQueue.push( info );
    }

    void SqrlClient::callSend( SqrlAction * action, SqrlString *url, SqrlString * payload ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_SEND;
        info->ptr = action;
        info->str[0] = new SqrlString( *url );
        info->str[1] = new SqrlString( *payload );
        this->callbackQueue.push( info );
    }

    void SqrlClient::callAsk( SqrlAction * action, SqrlString * message, SqrlString * firstButton, SqrlString * secondButton ) {
        struct CallbackInfo *info = new struct CallbackInfo();
        info->cbType = SQRL_CALLBACK_ASK;
        info->ptr = action;
        info->str[0] = new SqrlString( *message );
        info->str[1] = new SqrlString( *firstButton );
        info->str[2] = new SqrlString( *secondButton );
        this->callbackQueue.push( info );
    }

    SqrlClient::CallbackInfo::CallbackInfo() {
        this->cbType = 0;
        this->progress = 0;
        this->credentialType = SQRL_CREDENTIAL_PASSWORD;
        this->ptr = NULL;
        this->str[0] = NULL;
        this->str[1] = NULL;
        this->str[2] = NULL;
    }

    SqrlClient::CallbackInfo::~CallbackInfo() {
        for( int i = 0; i < 3; i++ ) {
            if( this->str[i] ) {
                delete this->str[i];
            }
        }
    }
}