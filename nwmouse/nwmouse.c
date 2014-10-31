/* 
 * Hack to convert mouse cursor usage to the hardware drawn mouse type
 * alegedly eliminates mouse lag when the frame rate gets low. 
 *
 * Originally based upon code started by Jens (lje) on the NWN forums. 
 *  
 * Copyright 2004, Jens (?) and David Holland, david.w.holland@gmail.com
 * 
 * Works by intercepting the SDL_ShowCursor() function to not 
 * shut off the mouse, when NWN asks for it. 
 * 
 * It also modifies the in memory copy of NWN to _not_ display
 * the mouse every frame. 
 * 
 * And it hooks the cursor change function to determine which
 * mouse cursor is being displayed.
 * 
 * Significant code recycled from NWMovies.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <math.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>


#include "nwmouse.h"

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

/* Function declarations */
void  		 NWMouse_setup_memory(unsigned int patch0, unsigned int patch1, unsigned int patch2, 
			unsigned int patch3, unsigned int patch4, unsigned int patch5, unsigned int patch6 );
void  		 NWMouse_printdata(char *ptr, int len);
void 		 NWMouse_memcpy(unsigned char *dest,  unsigned char *src, size_t n);
extern void 	 NWMouse_link1_asm(void);
extern void 	 NWMouse_link2_asm(void);
unsigned int	*NWMouse_findcookie(char *file);

void 		 NWMouse_Render(int dummy); 
void		 NWMouse_Texture(char *alpha1, char *dummy1, char *texture, char * dummy3);

typedef struct { 
	float	w; 
	float 	x; 
	float	y; 
	float	z; 
} Quaternion; 

Quaternion	NWMouse_Unit; 
float		M_180PI;
void		NWMouse_Orient(char *junk, char *junk2, Quaternion data); 
float		NWMouse_InnerProduct(Quaternion q1, Quaternion q2); 

char		NWMouse_current_cursor[PATH_MAX] = "gui_mp_defaultu"; 
int		NWMouse_cursor_changed;

int 		__nwmouse_cursor_state = 0;
int		__nwmouse_cursor_flag = 0; 

unsigned long 	__nwmouse_retaddr1 = 0x0;  /* modified by setup_memory() */
unsigned long 	__nwmouse_retaddr2 = 0x0;  /* modified by setup_memory() */
unsigned long	*NWMouse_orig_table;
unsigned long	*NWMouse_table; 

unsigned long   _NWMouse_REGISTER;
unsigned long	_NWMouse_ORIGTABLE; 
unsigned long	_NWMouse_INCD; 

int		_NWMouse_WinInit = 0; 
SDL_SysWMinfo	_NWMouse_WinInfo;

int (*__nwmouse_SDL_ShowCursor)(int) = NULL;
int   __nwmouse_enabled = 0; 

void NWMouse_logger(char *fmt, ...);

void NWMouse_Init(void) __attribute__((constructor));

void NWMouse_Init(void) {

	void		*self_handle; 
	void		*self_ptr; 
	char		*self_name_ptr; 

	Dl_info		info;

	struct	stat	statbuf;
	FILE		*fp;
	char		string1[80];
	char		string2[80];
	unsigned int	file_size, file_date; 

	unsigned int	patch0_addr, patch1_addr, patch2_addr, patch3_addr, patch4_addr, patch5_addr; 
	unsigned int 	patch6_addr; 
	unsigned int	*patch_address; 

	void		*__nwmouse_libSDL_handle;         /* Either a pointer to libSDL, or RTLD_NEXT, depending on which one "works" */

	__nwmouse_libSDL_handle = RTLD_NEXT;

	__nwmouse_SDL_ShowCursor = (int (*)())dlsym(__nwmouse_libSDL_handle, "SDL_ShowCursor");
        if( __nwmouse_SDL_ShowCursor == NULL ) { 
                /* via RTLD_NEXT failed, try loading libSDL directly. */
                __nwmouse_libSDL_handle = dlopen("libSDL-1.2.so.0", RTLD_NOW | RTLD_GLOBAL);
                if ( __nwmouse_libSDL_handle == NULL ) {
                        fprintf(stderr, "ERROR: NWMouse: dladdr(libSDL-1.2.so.0: _init): %s\n", dlerror()); 
                        abort(); 
                }
                __nwmouse_SDL_ShowCursor = dlsym(__nwmouse_libSDL_handle, "SDL_ShowCursor"); 
                if( __nwmouse_SDL_ShowCursor == NULL ) { 
                        fprintf(stderr, "ERROR: __nwmouse_SDL_ShowCursor == NULL: %s\n", dlerror()); 
                        abort(); 
                }
                fprintf(stderr, "NOTICE: NWMouse: using libSDL via direct dlopen()\n"); 
        } else { 
                fprintf(stderr, "NOTICE: NWMouse: Using libSDL via RTLD_NEXT.\n"); 
        }


	self_handle = dlopen("", RTLD_NOW | RTLD_GLOBAL); 
	self_ptr = dlsym(self_handle, "_init");
	if( self_ptr == NULL || dladdr(self_ptr, &info) <= 0 ) {
		fprintf(stderr, "ERROR: NWMouse: dladdr(self, _init): %s\n", dlerror()); 
		abort(); 
	} 
	self_name_ptr = basename((char *)info.dli_fname);
	if( strncmp( self_name_ptr, "nwmain", PATH_MAX) != 0 ) {
		dlclose(self_handle);
		return;
	}
	dlclose(self_handle);

	__nwmouse_enabled = 1; 

	/* Spit out a version number */
	fprintf(stderr, "NOTICE: NWMouse: Version: %s\n", _NWMOUSE_VERSION);

	if( stat("nwmain", &statbuf) != 0 ) { 
		fprintf(stderr, "ERROR: NWMouse: Unable to stat nwmain: %d: %s\n", errno, strerror(errno)); 
		exit(-1);
	}

	/* ini parsing.  No, this doesn't have a lot of error checking. */

	fp = fopen("nwmouse.ini", "r");
	if( fp == NULL ) {
		fprintf(stderr, "WARNING: NWMouse: No INI file.  Creating.\n");
		fp = fopen("nwmouse.ini", "w");
		if( fp == NULL ) {
			fprintf(stderr, "ERROR: NWMouse: Unable to create INI file.  Aborting: %d\n", errno);
			exit(-1);
		}
		fprintf(fp, "size 0\n");
		fprintf(fp, "time 0\n");
		fprintf(fp, "patch0 0\n");
		fprintf(fp, "patch1 0\n");
		fprintf(fp, "patch2 0\n");
		fprintf(fp, "patch3 0\n");
		fprintf(fp, "patch4 0\n");
		fprintf(fp, "patch5 0\n");
		fprintf(fp, "patch6 0\n");
		fclose(fp);
		fp = fopen("nwmouse.ini", "r");
		if( fp == NULL ) {
			fprintf(stderr, "ERROR: NWMouse: Unable to re-open nwmouse.ini. Aborting: %d\n", errno);
			exit(-1);
		}
	}
	while( fscanf(fp, "%s %s\n", string1, string2) != EOF ) {
		if( strcmp(string1, "size") == 0 ) {
			file_size = atoi(string2);
		}
		if( strcmp(string1, "time") == 0 ) {
			file_date = atoi(string2);
		}
		if( strcmp(string1, "patch0") == 0 ) {
			patch0_addr = strtol(string2, NULL, 0);
		}
		if( strcmp(string1, "patch1") == 0 ) {
			patch1_addr = strtol(string2, NULL, 0);
		}
		if( strcmp(string1, "patch2") == 0 ) {
			patch2_addr = strtol(string2, NULL, 0);
		}
		if( strcmp(string1, "patch3") == 0 ) {
			patch3_addr = strtol(string2, NULL, 0);
		}
		if( strcmp(string1, "patch4") == 0 ) {
			patch4_addr = strtol(string2, NULL, 0);
		}
		if( strcmp(string1, "patch5") == 0 ) {
			patch5_addr = strtol(string2, NULL, 0);
		}
		if( strcmp(string1, "patch6") == 0 ) {
			patch6_addr = strtol(string2, NULL, 0);
		}
	}
	fclose(fp);

	if(     statbuf.st_size != file_size || statbuf.st_mtime != file_date ) { 

		fprintf(stderr, "WARNING: NWMouse: INI recalculation required: %u:%u %u:%u\n",
			(unsigned int)statbuf.st_size, file_size, (unsigned int)statbuf.st_mtime, file_date); 

		patch_address = NWMouse_findcookie( "nwmain" );

		fp = fopen("nwmouse.ini", "w");
		if( fp == NULL ) {
			fprintf(stderr, "ERROR: NWMouse: Unable to create INI file.  Aborting: %d\n", errno);
			exit(-1);
		}
		fprintf(fp, "%s %lu\n", "size", (unsigned long)statbuf.st_size);
		fprintf(fp, "%s %lu\n", "time", (unsigned long)statbuf.st_mtime);
		fprintf(fp, "%s 0x%08x\n", "patch0", patch_address[0]);
		fprintf(fp, "%s 0x%08x\n", "patch1", patch_address[1]);
		fprintf(fp, "%s 0x%08x\n", "patch2", patch_address[2]);
		fprintf(fp, "%s 0x%08x\n", "patch3", patch_address[3]);
		fprintf(fp, "%s 0x%08x\n", "patch4", patch_address[4]);
		fprintf(fp, "%s 0x%08x\n", "patch5", patch_address[5]);
		fprintf(fp, "%s 0x%08x\n", "patch6", patch_address[6]);
		fclose(fp);
		fprintf(stderr, "NOTICE: NWMouse: INI File written: Now exiting.  This is perfectly normal!\n");
		fprintf(stderr, "NOTICE: Your next run of NWN should be complete, and include hardware mouse.\n");
		exit(0);
	}

	fprintf(stderr, "NOTICE: NWMouse: Patch 0 Address: 0x%08x\n", patch0_addr);
	fprintf(stderr, "NOTICE: NWMouse: Patch 1 Address: 0x%08x\n", patch1_addr);
	fprintf(stderr, "NOTICE: NWMouse: Patch 2 Address: 0x%08x\n", patch2_addr);
	fprintf(stderr, "NOTICE: NWMouse: Patch 3 Address: 0x%08x\n", patch3_addr);
	fprintf(stderr, "NOTICE: NWMouse: Patch 4 Address: 0x%08x\n", patch4_addr);
	fprintf(stderr, "NOTICE: NWMouse: Patch 5 Address: 0x%08x\n", patch5_addr);
	fprintf(stderr, "NOTICE: NWMouse: Patch 6 Address: 0x%08x\n", patch6_addr);

	NWMouse_setup_memory(patch0_addr, patch1_addr, patch2_addr, patch3_addr, patch4_addr, patch5_addr, patch6_addr);

	/* Initialize the unit Quaternion */ 
	NWMouse_Unit.w = 1.0;
	NWMouse_Unit.x = 0.0;
	NWMouse_Unit.y = 0.0;
	NWMouse_Unit.z = 0.0;

	M_180PI = 180.0 / M_PI; 

	fprintf(stderr, "NOTICE: NWMouse: Initialized.\n");

	return;
}

void NWMouse_setup_memory(unsigned int patch0, unsigned int patch1, unsigned int patch2, 
		unsigned int patch3, unsigned int patch4, unsigned int patch5, unsigned int patch6 )
{
        unsigned char   instruction[7];                         /* 5 byte instruction */
        unsigned long  	address_offset;

	unsigned int	*ptr; 
	int		max_functions, i;
	

	fprintf(stderr, "NOTICE: NWMouse: Stop pointer: 0x%08x\n", patch0); 
	fprintf(stderr, "NOTICE: NWMouse: Table Address: 0x%08x\n", patch2); 
	NWMouse_orig_table = (unsigned long *)patch2;
	ptr = (void *)patch2; 
	i = 1; 
	// fprintf(stderr, "DEBUG: NWMouse: Table: 0x%08x\n", ptr[i]); 
	while( ptr[i] != 0x0 && ptr[i] != patch0 ) { 
		i++; 
		// fprintf(stderr, "DEBUG: NWMouse: Table: 0x%08x\n", (int *)ptr[i]); 
	}
	fprintf(stderr, "NOTICE: NWMouse: Table Length: %d\n", i); 
	NWMouse_table = (unsigned long *)malloc( i * sizeof(unsigned long *)); 
	if( NWMouse_table == NULL ) { 
		fprintf(stderr, "ERROR: NWMouse: Unable to allocate memory.\n"); 
		abort(); 
	}
	memcpy((void *)&NWMouse_table[0], (void *)patch2, i * sizeof(unsigned long *)); 
	max_functions = i; 
	i = 0;
	while( i < max_functions ) { 
		if( NWMouse_table[i] == patch3 ) { 
			NWMouse_table[i] = (unsigned long)&NWMouse_Render; 
			fprintf(stderr, "NOTICE: NWMouse: Render function replaced in secondary table, offset: %d\n", i); 
		}
		if( NWMouse_table[i] == patch4 ) { 
			NWMouse_table[i] = (unsigned long)&NWMouse_Texture; 
			fprintf(stderr, "NOTICE: NWMouse: Texture function replaced in secondary table, offset: %d.\n", i); 
		}
		if( NWMouse_table[i] == patch6 ) { 
			NWMouse_table[i] = (unsigned long)&NWMouse_Orient; 
			fprintf(stderr, "NOTICE: NWMouse: Orientation function replaced in secondary table, offset: %d.\n", i); 
		}
		i++;
	}

	memcpy((void *)&_NWMouse_ORIGTABLE, (unsigned char *)patch1 + 33, 4); 
	memcpy((void *)&_NWMouse_INCD, (unsigned char *)patch5 - 7, 4); 

	fprintf(stderr, "NOTICE: NWMouse: Original Table: "); 
        NWMouse_printdata((char *)patch1 + 33, 4);
        fprintf(stderr, "\n");

	fprintf(stderr, "NOTICE: NWMouse: Pushed: "); 
        NWMouse_printdata((char *)patch5 - 18, 4);
        fprintf(stderr, "\n");

/* First Mod */
        fprintf(stderr, "NOTICE: NWMouse: PreMod1: ");
        NWMouse_printdata((void *)patch1 + 30, 7);
        fprintf(stderr, "\n");

        address_offset = (unsigned long) &NWMouse_link1_asm;		/* First == Assembler Code in .S */
        address_offset = address_offset - (unsigned long) (patch1 + 30) - 5; /* How many bytes should the jump be */
        memcpy(instruction + 1, &address_offset, 4);
        instruction[0] = '\xe9';
	instruction[5] = '\x90'; 
	instruction[6] = '\x90'; 
        NWMouse_memcpy((void *)patch1 + 30, instruction, 7);				/* Put the jump in */
        __nwmouse_retaddr1 = (unsigned long)(patch1 + 30) + 0x7;			/* setup return address */

        fprintf(stderr, "NOTICE: NWMouse: PostMod1: ");
        NWMouse_printdata((void *)patch1 + 30, 7);
        fprintf(stderr, "\n");
/* Second Mod */
        fprintf(stderr, "NOTICE: NWMouse: PreMod2: ");
        NWMouse_printdata((void *)patch5 - 9, 6);
        fprintf(stderr, "\n");

        address_offset = (unsigned long) &NWMouse_link2_asm;		/* First == Assembler Code in .S */
        address_offset = address_offset - (unsigned long) (patch5-9) - 5; /* How many bytes should the jump be */
        memcpy(instruction + 1, &address_offset, 4);
        instruction[0] = '\xe9';
        instruction[5] = '\x90';
        NWMouse_memcpy((void *)patch5-9, instruction, 5);				/* Put the jump in */
        __nwmouse_retaddr2 = (unsigned long)(patch5-9) + 0x6;			/* setup return address */

        fprintf(stderr, "NOTICE: NWMouse: PostMod2: ");
        NWMouse_printdata((void *)patch5-9, 7);
        fprintf(stderr, "\n");

}

void NWMouse_printdata(char *ptr, int len)
{
        int i;

        for(i=0; i<len; i++) {
                fprintf(stderr, "%02x ", (unsigned char) ptr[i]);
        }
        return;
}

void NWMouse_memcpy(unsigned char *dest, unsigned char *src, size_t n)
{
        unsigned char *p = dest;

        /* Align to a multiple of PAGESIZE, assumed to be a power of two */
        /* Do two pages, just to make certain we get a big enough chunk */
        p = (unsigned char *)(((int) p + PAGESIZE-1) & ~(PAGESIZE-1));
        if( mprotect(p-PAGESIZE, 2 * PAGESIZE, PROT_READ|PROT_WRITE|PROT_EXEC) != 0 ) {
                fprintf(stderr, "ERROR: NWMouse: Could not de-mprotect(%p)\n", p);
                exit(-1);
        }

        memcpy(dest, src, n);
        /* restore memory protection */
        if( mprotect(p-PAGESIZE, 2 * PAGESIZE, PROT_READ|PROT_EXEC) != 0 ) {
                fprintf(stderr, "ERROR: NWMouse: Could not re-mprotect(%p)\n", p);
                exit(-1);
        }
}

int SDL_ShowCursor(int toggle) {

	if( ! __nwmouse_enabled ) { 
		return(__nwmouse_SDL_ShowCursor(toggle)); 
	}

	NWMouse_logger("NOTICE: NWMouse: SDL_ShowCursor(%d)\n", toggle); 

	if (toggle == SDL_QUERY) {
		return(__nwmouse_cursor_state);
	}
	__nwmouse_SDL_ShowCursor(SDL_ENABLE);
	__nwmouse_cursor_state = toggle;
	return(__nwmouse_cursor_state);
}


void NWMouse_logger(char *fmt, ...) {
        va_list arg_list;
        static  FILE    *nwu_log = NULL;
	struct	timeval	tv;

	gettimeofday(&tv, NULL); 
	if( ! nwu_log ) {
		nwu_log = fopen("nwmouse.log", "a");
	}
	va_start(arg_list, fmt);
	if( nwu_log ) {
		fprintf(nwu_log, "%ld.%06ld: ", tv.tv_sec, tv.tv_usec); 
		vfprintf(nwu_log, fmt, arg_list);
	} else {
		fprintf(stderr, "%ld.%06ld: ", tv.tv_sec, tv.tv_usec); 
		vfprintf(stderr, fmt, arg_list);
	}
	va_end(arg_list);
	if( nwu_log ) fclose(nwu_log); 
	nwu_log = NULL; 

        return;
}

/* And do we really want to Render the mouse cursor?  I don't think so */

void NWMouse_Render(int dummy) { 
	int		ret; 
	Cursor		X11_cursor;

	/* NWMouse_logger("NOTICE: NWMouse: Render function called.\n");  */

	// NWMouse_logger("NOTICE: NWMouse: Texture function called.\n"); 
	if( NWMouse_cursor_changed ) { 
		// NWMouse_logger("NWMOUSE: Notice: Mouse changed to: %s\n", NWMouse_current_cursor); 
	
		if( _NWMouse_WinInit == 0 ) { 
			_NWMouse_WinInfo.version.major = 1;
			ret = SDL_GetWMInfo(&_NWMouse_WinInfo);
			if(ret != 1) {
				NWMouse_logger("ERROR: NWMouse: SDL_ShowCursor: Could not get WinInfo: %s\n", SDL_GetError()); 
				exit(1);
			}
			_NWMouse_WinInit = 1; 
		}
	
		_NWMouse_WinInfo.info.x11.lock_func();
		X11_cursor = XcursorLibraryLoadCursor(_NWMouse_WinInfo.info.x11.display, NWMouse_current_cursor);
		if( X11_cursor == 0 ) { 
			NWMouse_logger("WARNING: NWMouse: Unable to load cursor: %s\n", NWMouse_current_cursor); 
		}
		XDefineCursor(_NWMouse_WinInfo.info.x11.display, _NWMouse_WinInfo.info.x11.window, X11_cursor);
		_NWMouse_WinInfo.info.x11.unlock_func();

		NWMouse_cursor_changed = 0; 
	}

	return; 
}

/* My version of the Mouse object texturing routine.  */

void NWMouse_Texture(char *alpha1, char *dummy1, char *texture, char *dummy3) {

	if( texture != NULL ) {
		strncpy(NWMouse_current_cursor, texture, PATH_MAX); 
		NWMouse_cursor_changed = 1; 
	}

	return; 
}

/* Perhaps not proper utilization of NWN's Quaternions 
   but it should give us an angle for rotating the cursor */

float NWMouse_InnerProduct(Quaternion q1, Quaternion q2) { 

	float	x,y,z,w; 

	x = q1.x * q2.x; 
	y = q1.y * q2.y; 
	z = q1.z * q2.z; 
	w = q1.w * q2.w; 

	return( x + y + z + w ); 
}

void NWMouse_Orient(char *junk, char *junk2, Quaternion data) { 
	float 	angle; 			/* Sorry, I work in boring ol' degrees */
	int	section; 		/* Presume 16 cursors for a circle */
	char	texture[16]; 

	angle = (acosf( NWMouse_InnerProduct( data, NWMouse_Unit )) * M_180PI) * 2; 
	if( data.z > 0.0 && data.w >= 0.0 ) { 
		angle = 360 - angle; 
	} 
	section = floor( angle / 22.5 ); 
	if( section > 15 ) { section = 0; } 

	// NWMouse_logger("quaternion x: %f, y: %f, z: %f, w: %f, t: %f, s: %d c: %s \n", data.x, data.y, data.z, data.w, angle, section, NWMouse_current_cursor); 
	if( !strncmp(NWMouse_current_cursor, "gui_mp_arrun", 12) ) { 
		sprintf(texture, "gui_mp_arrun%02d", section); 
		NWMouse_Texture(NULL, NULL, texture, NULL); 
	} else if( !strncmp(NWMouse_current_cursor, "gui_mp_arwalk", 13) ) { 
		sprintf(texture, "gui_mp_arwalk%02d", section); 
		NWMouse_Texture(NULL, NULL, texture, NULL); 
	}
	return; 
}

/* Override Mouse object creation tables */

void NWMouse_link1_c(void) { 
        char    object[PATH_MAX];
	int	ret; 

	if( _NWMouse_REGISTER != 0 ) { 
		strncpy(object, (char *)_NWMouse_REGISTER, PATH_MAX ); 
	} else {
		strcpy(object, ""); 
	}

	// NWMouse_logger("NOTICE: NWMouse: link1 Called: %s\n", object); 
	if( strcmp(object, "GUI_MOUSE") == 0 ) { 
		// NWMouse_logger("DEBUG: NWMouse: Proper object detected...Prepare for action in second routine.\n"); 
		__nwmouse_cursor_flag = 1; 
	} 

	return; 
}

void NWMouse_link2_c(void) { 
        unsigned long 	object; 
	unsigned long	*ptr;
	int	tmp; 
	int	i; 

	object = _NWMouse_REGISTER; 
	ptr = (unsigned long *)object; 
	// NWMouse_logger("DEBUG: NWMouse: link2 Called: %08x\n", object); 
	if( __nwmouse_cursor_flag ) { 
		__nwmouse_cursor_flag = 0; 
		// NWMouse_logger("DEBUG: NWMouse: Searching for Object table...\n"); 
		tmp = 0; 
		for(i = 0; i<10; i++ ) { 
			// NWMouse_logger("DEBUG: NWMouse: Object table search: 0x%08x - 0x%08x\n", 
			//	(unsigned long)NWMouse_orig_table, (unsigned long)ptr[i]); 
			if( ptr[i] == (unsigned long)NWMouse_orig_table ) { 
				break; 
			}
		}
		if( i == 10 ) { 
			NWMouse_logger("ERROR: NWMouse: Object table not located.\n"); 
			return; 
		} 
		ptr[i] = (unsigned long)NWMouse_table; 
	}

	return; 
}
