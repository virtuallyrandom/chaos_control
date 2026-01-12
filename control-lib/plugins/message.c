/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "../ring.h"
#include "../thread.h"
#include "message.h"
#include "plugin.h"
#include "time.h"

#define PLUGIN_MESSAGE_CHECKSUM 0x4d45535341474520 /* 'MESSAGE ' */

#define MAX_CALLBACKS	32

#define RING_SIZE ( 16 * 1024 * 1024 )

#define SYSTEM_ID	0
#define PACKET_ID	2

const char PLUGIN_NAME_MESSAGE[] = "Message";

typedef struct _messageCallback_t {
	onMessageCallback	cb;
	void*				param;
} messageCallbackInfo_t;

typedef struct _messageData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct messageInterface_type	messageInterface;
	struct systemInterface_type *	systemInterface;
	struct timeInterface_type *		timeInterface;
	messageCallbackInfo_t			onMessageCB[ MAX_CALLBACKS ];
	size_t							onMessageCount;
	struct ringBuffer_type *		ring;
#if defined( _WIN32 )
	HANDLE							output;
#else /* !defined( _WIN32 ) */
	FILE*							output;
#endif /* !defineD( _WIN32 ) */
} messageData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static messageData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MESSAGE_CHECKSUM ) {
			return ( messageData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
frametimeInterfaceToMe
========================
*/
static messageData_t* messageInterfaceToMe( struct messageInterface_type * const iface ) {
	const uint8_t * const ptrToMessageInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToMessageInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_MESSAGE_CHECKSUM ) {
		return ( messageData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
registerOnMessage
========================
*/
static void registerOnMessage(	struct messageInterface_type * const iface,
								onMessageCallback cb,
								void * const param ) {
	size_t i;
	messageData_t * const me = messageInterfaceToMe( iface );

	if ( me == NULL ) {
		return;
	}

	for ( i = 0; i < me->onMessageCount; ++i ) {
		messageCallbackInfo_t * const info = me->onMessageCB + i;
		if ( info->cb == 0 ) {
			info->cb = cb;
			info->param = param;
			return;
		}
	}

	if ( me->onMessageCount < MAX_CALLBACKS ) {
		messageCallbackInfo_t * const info = me->onMessageCB + me->onMessageCount++;
		info->cb = cb;
		info->param = param;
	}
}

/*
========================
unregisterOnMessage
========================
*/
static void unregisterOnMessage(	struct messageInterface_type * const iface,
									onMessageCallback cb,
									void * const param ) {
	size_t i;
	messageData_t * const me = messageInterfaceToMe( iface );

	if ( me == NULL ) {
		return;
	}

	for ( i = 0; i < me->onMessageCount; ++i ) {
		messageCallbackInfo_t * const info = me->onMessageCB + i;
		if ( info->cb == cb && info->param == param ) {
			info->cb = NULL;
			info->param = NULL;
		}
	}
}

/*
========================
onPacket
========================
*/
static void myUpdate( struct pluginInterface_type * const self );
static void onPacket( messageData_t * const me, const struct packetHeader_type * const header ) {
	struct pkt_t {
		struct packetHeader_type header;
		const char string[ 256 ];
	} * const pkt = ( struct pkt_t* )header;

	void *ptr;
	int ms;
	int ss;
	int mm;
	int hh;
	size_t size;
	const size_t length = ( size_t )header->size - sizeof( struct packetHeader_type );
	const size_t blockSize = length + 13;

	( void )me;

	while ( !RingWriteLock( me->ring, blockSize, &ptr, &size ) ) {
		myUpdate( &me->pluginInterface );
		ThreadYield();
	}

	size = ( size_t )me->timeInterface->convertTimeToMilliseconds( me->timeInterface, header->time );

	ms = ( int )( size % 1000 );
	size /= 1000;

	ss = ( int )( size % 60 );
	size /= 60;

	mm = ( int )( size % 60 );
	size /= 60;

	hh = ( int )( size % 60 );

	sprintf( ptr, "%02d.%02d.%02d.%03d %.*s", hh, mm, ss, ms, ( uint32_t )length, pkt->string );
	RingWriteUnlock( me->ring, blockSize );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	messageData_t * const me = pluginInterfaceToMe( self );
	char path[ PATH_MAX ];

	if ( me == NULL ) {
		return;
	}

	me->ring = RingCreate( RING_SIZE );

	me->timeInterface = ( struct timeInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_TIME );

	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_ID, PACKET_ID, onPacket, me );

	me->systemInterface->describeDataSource(	me->systemInterface,
												path,
												sizeof( path ),
												"remo.%K.%P.%T.%R.txt" );
	path[ sizeof( path ) - 1 ] = 0;

#if defined( _WIN32 )
	me->output = CreateFileA( path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
#else /* !defined( _WIN32 ) */
	me->output = fopen( path, "w+" );
#endif /* !defined( _WIN32 ) */
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	messageData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_ID, PACKET_ID, onPacket, me );

#if defined( _WIN32 )
	if ( me->output != INVALID_HANDLE_VALUE ) {
		CloseHandle( me->output );
		me->output = INVALID_HANDLE_VALUE;
	}
#else /* !defined( _WIN32 ) */
	if ( me->output != NULL ) {
		fclose( output );
		output = NULL;
	}
#endif /* !defined( _WIN32 ) */

	RingDestroy( me->ring );
}

/*
========================
myUpdate
========================
*/
static void myUpdate( struct pluginInterface_type * const self ) {
	const char*	buffer;
	size_t		avail;
	size_t		i;

	messageData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	avail = RingReadAvail( me->ring );
	if ( avail == 0 ) {
		return;
	}

	if ( RingReadLock( me->ring, avail, &buffer, &avail ) == 0 ) {
		return;
	}

#if defined( _WIN32 )
	if ( me->output != INVALID_HANDLE_VALUE ) {
		DWORD wrote;
#else /* !defined( _WIN32 ) */
	if ( me->output != NULL ) {
#endif /* !defined( _WIN32 ) */

		for ( i = 0; i < avail; ++i ) {
			switch ( buffer[ i ] ) {
				case '\r':
					break;

				case '\n':
#if defined( _WIN32 )
					WriteFile( me->output, "\r\n", 2, &wrote, NULL );
#else /* !defined( _WIN32 ) */
					fwrite( buffer, "\r\n", 2, me->output );
#endif /* !defined( _WIN32 ) */
					break;

				default:
#if defined( _WIN32 )
					WriteFile( me->output, &buffer[ i ], 1, &wrote, NULL );
#else /* !defined( _WIN32 ) */
					fwrite( buffer, &buffer[ i ], 1, me->output );
#endif /* !defined( _WIN32 ) */
					break;
			}
		}
	}

	for ( i = 0; i < me->onMessageCount; ++i ) {
		messageCallbackInfo_t * const info = me->onMessageCB + i;
		if ( info->cb ) {
			info->cb( info->param, buffer, avail );
		}
	}

	RingReadUnlock( me->ring, avail );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_MESSAGE;
}

/*
========================
MemoryGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	messageData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->messageInterface;
}

/*
========================
Message_Create
========================
*/
struct pluginInterface_type * Message_Create( struct systemInterface_type * const sys ) {
	messageData_t * const me = sys->allocate( sys, sizeof( messageData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( messageData_t ) ) ;

	me->checksum = PLUGIN_MESSAGE_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.update				= myUpdate;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->messageInterface.registerOnMessage		= registerOnMessage;
	me->messageInterface.unregisterOnMessage	= unregisterOnMessage;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
