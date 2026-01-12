/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __TIME_H__
#define __TIME_H__

struct systemInterface_type;
struct pluginInterface_type;

struct timeInterface_type {
	double ( *convertTimeToClocks					)( struct timeInterface_type * const, const uint64_t time );
	double ( *convertTimeToMicroseconds				)( struct timeInterface_type * const, const uint64_t time );
	double ( *convertTimeToMilliseconds				)( struct timeInterface_type * const, const uint64_t time );
	double ( *convertTimeToSeconds					)( struct timeInterface_type * const, const uint64_t time );

	double ( *convertTimeToClocksForFrequency		)( struct timeInterface_type * const, const uint64_t time, const uint64_t cpuFrequency );
	double ( *convertTimeToMicrosecondsForFrequency	)( struct timeInterface_type * const, const uint64_t time, const uint64_t cpuFrequency );
	double ( *convertTimeToMillisecondsForFrequency	)( struct timeInterface_type * const, const uint64_t time, const uint64_t cpuFrequency );
	double ( *convertTimeToSecondsForFrequency		)( struct timeInterface_type * const, const uint64_t time, const uint64_t cpuFrequency );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_TIME[];
struct pluginInterface_type * Time_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __TIME_H__ */
