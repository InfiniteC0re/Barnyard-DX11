#include "pch.h"
#include "Hash.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

const TUINT32 hash_32_fnv1a( const void* key, const TUINT32 len )
{
	const char* data  = (char*)key;
	TUINT32     hash  = 0x811c9dc5;
	TUINT32     prime = 0x1000193;

	for ( TUINT32 i = 0; i < len; ++i )
	{
		uint8_t value = data[ i ];
		hash          = hash ^ value;
		hash *= prime;
	}

	return hash;
}

const TUINT64 hash_64_fnv1a( const void* key, const TUINT64 len )
{
	const char* data  = (char*)key;
	TUINT64     hash  = 0xcbf29ce484222325;
	TUINT64     prime = 0x100000001b3;

	for ( TUINT32 i = 0; i < len; ++i )
	{
		uint8_t value = data[ i ];
		hash          = hash ^ value;
		hash *= prime;
	}

	return hash;
}
