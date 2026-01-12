
/*
================================================================================================
This file only generates the precompiled header.  Don't add code here.
================================================================================================
*/

void myMemcpy( void * const dst, const void * const src, const size_t count ) {
	size_t i;

	for ( i = 0; i < count; i++ ) {
		((char*)dst)[i] = ((char*)src)[i];
	}
}

