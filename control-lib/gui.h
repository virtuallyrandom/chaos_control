#ifndef __GUI_H__
#define __GUI_H__

struct gui_type;
struct dataSourceInterface_type;

struct _listData_t;

typedef LRESULT ( __stdcall *windowProc_t )( HWND, UINT, WPARAM, LPARAM );

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct gui_type *	GUI_Create(	void ( *cb )( void * const, struct dataSourceInterface_type * const ),
								void * const param );
void				GUI_Destroy( struct gui_type * const );
int32_t				GUI_Begin( struct gui_type * const );
void				GUI_End( struct gui_type * const );
window_t			GUI_CreateWindow( struct gui_type * const, const char * const title, windowProc_t, void * const param );
void				GUI_DestroyWindow( struct gui_type * const, window_t );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __GUI_H__ */

