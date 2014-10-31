#define _GNU_SOURCE		/* Needed so dlfcn.h defines the right stuff */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <dlfcn.h>
#include <errno.h>

#include <sys/mman.h>
#include <limits.h>

#include <elf.h>
#include <libelf.h>

#include <link.h>

#include "libdis.h"

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#define NWM_PROTECTED 1
#define NWM_UNPROTECTED 0

unsigned int *NWMouse_search(unsigned char *buffer_ptr, unsigned char *entry, int sh_size, char *cookie[], int start, int count, int my_abort);

static char *NWMouse_cookie0[] = { 
	"push","push","call","add","push","push","push","push","call","add","push","push",
	"push","push","call","add","push","push","push","push", NULL
};

static char *NWMouse_cookie1[] = { 
	"push","push","push","sub","push","push","mov","call","pop","pop","mov","mov", NULL
};

static char *NWMouse_cookie2[] = { 
	"push","sub","mov","mov","test","jz","sub","push","call","add","cmp", "jz", "fld", NULL
};

static char *NWMouse_cookie3[] = { 
	"push","push","push","sub","mov","cmp","jnz","sub","push","lea", NULL
}; 

static char *NWMouse_cookie4[] = { 
	"push","push","push","push","call","inc","add","jmp",NULL
}; 

static char *NWMouse_cookie5[] = { 
	"push", "mov", "mov", "push", "push", "lea", "lea", "cld", "mov", "rep:movsd", "mov", "lea",
	"mov", "rep:movsd", "pop", "mov", "pop", "pop", "ret", NULL 
}; 
static int NWMouse_cookie5_size = 52; 

static int(*NWMouse_disassemble_init_ptr)(int, int); 
static int(*NWMouse_disassemble_address_ptr)(char *, struct instr *);
static int(*NWMouse_disassemble_cleanup_ptr)(void); 

unsigned int *NWMouse_findcookie(char *filename)
{
	Elf			*elf_ptr; 
	int			fd; 
	Elf32_Ehdr		*ehdr; 
	Elf_Scn			*section; 
	Elf32_Shdr		*section_header;
	Elf32_Shdr		*code_header;
	unsigned char		*buffer, *entry; 
	unsigned char		*buffer_ptr; 
	unsigned char		*cookie_address; 
	struct		stat	statbuf; 

	static	unsigned int	calls[7]; 			/* Return things in this array */
	unsigned int		*ptr_calls; 
	int			instruction_size;
	struct 		instr 	instruction;

	int			i, j; 
	unsigned int		*table_ptr;
	int			done; 

	/* Dynamic Linking, we only use this stuff if we need it */

	void			*dlhandle; 

	dlhandle = dlopen("nwmouse/libdis/libdisasm.so", RTLD_NOW); 
	if( !dlhandle ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) dlopen of libdisasm.so failed: %s\n", dlerror()); 
		abort(); 
	}

	NWMouse_disassemble_init_ptr = (int (*)())dlsym(dlhandle, "disassemble_init"); 
	if( NWMouse_disassemble_init_ptr == NULL ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) dlsym(disassemble_init) failed: %s\n", dlerror()); 
		abort(); 
	}
	NWMouse_disassemble_address_ptr = (int (*)())dlsym(dlhandle, "disassemble_address"); 
	if( NWMouse_disassemble_address_ptr == NULL ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) dlsym(disassemble_address) failed: %s\n", dlerror()); 
		abort(); 
	}
	NWMouse_disassemble_cleanup_ptr = (int (*)())dlsym(dlhandle, "disassemble_cleanup"); 
	if( NWMouse_disassemble_cleanup_ptr == NULL ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) dlsym(disassemble_cleanup) failed: %s\n", dlerror()); 
		abort(); 
	}
	NWMouse_disassemble_init_ptr(0, INTEL_SYNTAX); 
		
/* Initialize the elves. */

	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "ERROR: NWMouse: (cookie) libelf version mismatch.\n"); 
		abort(); 
	}

/* open library */ 	

	fd = open(filename, O_RDONLY); 
	if( fd < 0 ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Unable to open shared library: %s (%d)\n", filename, errno); 
		abort(); 
	}
	if( fstat(fd, &statbuf) < 0 ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Unable to stat shared library: %s (%d) Howd that happen?\n", filename, errno); 
		abort(); 
	}
	buffer = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if( buffer == NULL ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Unable to mmap executable: %s (%d)\n", filename, errno); 
		abort(); 
	}
	elf_ptr = elf_begin(fd, ELF_C_READ, (Elf *)0);
	if( elf_ptr == NULL) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) elf_begin failed: %s.\n", elf_errmsg(elf_errno())); 
		abort(); 
	} 

	/* Get the Header */
	if ( (ehdr = elf32_getehdr(elf_ptr)) == NULL) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Unable to get Elf header: %s\n",  elf_errmsg(elf_errno()) ); 
		abort(); 
	}
	section = 0; 
	code_header = NULL; 
	while( (section = elf_nextscn( elf_ptr, section )) ) { 
		section_header = elf32_getshdr(section); 
		if( 	ehdr->e_entry >= section_header->sh_addr &&
			ehdr->e_entry < (section_header->sh_addr + section_header->sh_size)) {
				code_header = section_header; 
				break; 
		}
	}
	if( code_header == NULL ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Unable to locate appropriate code section.\n"); 
		abort(); 
	}

	/* Found start of program */
	entry = (unsigned char *)ehdr->e_entry - (code_header->sh_addr - code_header->sh_offset);
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Entry point determined: %p\n", entry); 
	buffer_ptr = (unsigned char *) (int)entry + (int)buffer; 

	calls[0] = (NWMouse_search(buffer_ptr, (unsigned char *)ehdr->e_entry, code_header->sh_size, NWMouse_cookie0, 0, -1, 1))[0]; 
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie0 location: 0x%08x\n", (unsigned int)calls[0] + (unsigned int)code_header->sh_addr);

	ptr_calls = NWMouse_search(buffer_ptr, (unsigned char *)ehdr->e_entry, code_header->sh_size, NWMouse_cookie1, 0, 1, 1);

	calls[1] = ptr_calls[0]; 
	calls[2] = ptr_calls[1]; 

	instruction_size = NWMouse_disassemble_address_ptr((char *)buffer_ptr + calls[2], &instruction ); 
	if( ! instruction_size ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Curious: Invalid instruction disassembled: %08x\n", calls[2]); 
		fprintf(stderr, "ERROR: NWMouse: (cookie) This really shouldn't happen.\n"); 
		fprintf(stderr, "ERROR: NWMouse: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort();
	}
	if( strcmp(instruction.mnemonic, "mov") || strtol( instruction.src, (char **)NULL, 16) < code_header->sh_addr ) { 
		fprintf(stderr, "ERROR: NWMouse: (cookie) Unexpected opcode found: %s %s\n", instruction.mnemonic, instruction.src); 
		fprintf(stderr, "ERROR: NWMouse: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		abort();
	}
	calls[2] = strtol( instruction.src, (char **)NULL, 16);
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie1 location: 0x%08x\n", (unsigned int)calls[1] + (unsigned int)code_header->sh_addr);
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie2 location: 0x%08x\n", (unsigned int)calls[2]); 

	calls[3] = (NWMouse_search(buffer_ptr, (unsigned char *)ehdr->e_entry, code_header->sh_size, NWMouse_cookie2, 0, 1, 1))[0]; 
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie3 location: 0x%08x\n", (unsigned int)calls[3] + (unsigned int)code_header->sh_addr);

	calls[4] = (NWMouse_search(buffer_ptr, (unsigned char *)ehdr->e_entry, code_header->sh_size, NWMouse_cookie3, 0, 1, 1))[0]; 
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie4 location: 0x%08x\n", (unsigned int)calls[4] + (unsigned int)code_header->sh_addr);

	calls[5] = (NWMouse_search(buffer_ptr, (unsigned char *)ehdr->e_entry, code_header->sh_size, NWMouse_cookie4, 0, 1, 1))[1]; 
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie5 location: 0x%08x\n", (unsigned int)calls[5] + (unsigned int)code_header->sh_addr);

/* Calls "loaded".  Correct into virtual addresses */

	calls[0] = calls[0] + code_header->sh_addr; 		/* Function outside of end of table */
	calls[1] = calls[1] + code_header->sh_addr; 		/* Constructor */
	// calls[2] = calls[2] + code_header->sh_addr;  - Do not add to this one - Already corrected. /* Table Start */
	calls[3] = calls[3] + code_header->sh_addr; 		/* Render */
	calls[4] = calls[4] + code_header->sh_addr; 		/* Texture */
	calls[5] = calls[5] + code_header->sh_addr; 		/* Constructed object */

/* Find the Orientation function, ugh... */
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Searching for proper Orientation function, this may take a while...\n"); 

/* Partially cloned from nwmouse.c */ 
	table_ptr = (void *)calls[2];
        i = 1; done = 0;
        while( table_ptr[i] != 0x0 && table_ptr[i] != calls[0] && !done ) {
		calls[6] = 
			(NWMouse_search(buffer_ptr, 			/* Secondary in memory copy 		*/
				(unsigned char *)ehdr->e_entry,		/* Kinda useless, unless _NOTDEF_ defd	*/
				table_ptr[i] - code_header->sh_addr + NWMouse_cookie5_size,	/* routine length. - hardcoded - blah 	*/
				NWMouse_cookie5, 			/* Search cookie			*/
				table_ptr[i] - code_header->sh_addr, 	/* start position 			*/
				1,					/* First occurance 			*/
				0					/* Abort if not found? 			*/
			))[0]; 
		if( calls[6] != 0 ) { 
			j = 0; 
			while( j < NWMouse_cookie5_size ) { 
				memset(&instruction, 0, sizeof(struct code));
				instruction_size = NWMouse_disassemble_address_ptr((char *)buffer_ptr + calls[6] + j, &instruction );
				if( !instruction_size ) { 

			fprintf(stderr, "ERROR: NWMouse: (cookie) Huh...: Invalid instruction disassembled: %08x\n", calls[2]); 
			fprintf(stderr, "ERROR: NWMouse: (cookie) This really shouldn't happen.\n"); 
			fprintf(stderr, "ERROR: NWMouse: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 

					abort();

				}
				if( !strcmp(instruction.mnemonic, "mov") && 
					strtol( instruction.src, (char **)NULL, 16) == 0x4 &&
					!strcmp(instruction.dest, "ecx") ) { 
					done = 1; 
					j = NWMouse_cookie5_size; 
				}
				j += instruction_size; 
			}
		}
		i++; 
        }
	fprintf(stderr, "NOTICE: NWMouse: (cookie) Cookie6 location: 0x%08x\n", (unsigned int)calls[6] + (unsigned int)code_header->sh_addr);
	calls[6] = calls[6] + code_header->sh_addr; 		/* Set Orientation */

	elf_end(elf_ptr); 
	munmap(buffer, statbuf.st_size ); 
	close(fd); 
	dlclose(dlhandle); 
	return(calls); 
}

unsigned int *NWMouse_search(unsigned char *buffer_ptr, unsigned char *entry, int sh_size, char *cookie[], int start, int count, int my_abort) {

	struct 		instr 	current_instruction;
	unsigned char		*cookie_address; 
	unsigned char		*func_start; 
	unsigned char		*current_func_start; 
	int			matches_found; 
	int			i, instruction_size, pct_complete;
	static unsigned int	retarray[2]; 
	int			xx; 
	int			current_count = 0; 

	i=start; 
	matches_found = 0; 
	cookie_address = NULL; 
	func_start = NULL; 
	pct_complete = ((float)i / (sh_size)) * 100.0; 
	if( my_abort ) {
		fprintf(stderr, "NOTICE: NWMouse: (cookie) Searching executable: %02d", (int)pct_complete); 
	}
	while( i < sh_size && current_count != count ) { 
		pct_complete = ((float)i / (sh_size)) * 100.0; 
		if( ((int)pct_complete % 4) == 0 && my_abort ) { 
			printf("%02d", (int)pct_complete); 
		}
		memset(&current_instruction, 0, sizeof(struct code));
		instruction_size = NWMouse_disassemble_address_ptr((char *)buffer_ptr + i, &current_instruction ); 
		if( instruction_size ) { 

#ifdef _NOTDEF_
			printf("%08x: ", (unsigned int)i+(unsigned int)entry); 
				for (xx = 0; xx < 12; xx++) {
					if (xx < instruction_size) printf("%02x ", buffer_ptr[i + xx]);
					else printf("   ");
				}

				printf("%s", current_instruction.mnemonic);
				if (current_instruction.dest[0] != 0) printf("\t%s", current_instruction.dest);
				if (current_instruction.src[0] != 0) printf(", %s", current_instruction.src);
				if (current_instruction.aux[0] != 0) printf(", %s", current_instruction.aux);
				printf("\n");
#endif
			if( ! strcmp(current_instruction.mnemonic, "push") && current_instruction.dest[0] != 0 && 
					! strcmp(current_instruction.dest, "ebp" )) { 
				current_func_start = (unsigned char *)i; 
			}
			
			if( strcmp(current_instruction.mnemonic, (char *)cookie[matches_found]) == 0 ) { 
				matches_found++; 
				if( cookie[matches_found] == NULL ) { 
					func_start = current_func_start; 

					cookie_address = (unsigned char *)i; 
					current_count ++; 
					matches_found = 0; 
				}
			} else { 
				matches_found = 0; 
			}
			i += instruction_size; 
		} else { 
			fprintf(stderr, "\nERROR: NWMouse: (cookie) Invalid instruction disassembled: %08x\n", i); 
			fprintf(stderr, "ERROR: NWMouse: (cookie) Probably a bug in libdis.\n"); 
			fprintf(stderr, "ERROR: NWMouse: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
			abort();
		}
	}
	if( my_abort ) { 
		fprintf(stderr, "\n"); /* Clean up after percent display */
	}
	if( cookie_address == NULL && my_abort ) { 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMouse: (cookie) Magic cookie not found.\n"); 
		fprintf(stderr, "SERIOUS FATAL ERROR: NWMouse: (cookie)    Please contact David Holland (david.w.holland@gmail.com)\n"); 
		fprintf(stderr, "NOTICE: %s", cookie[0]); 
		i = 1; 
		while( cookie[i] != NULL ) { 
			fprintf(stderr, ", %s", cookie[i]); 
			i++; 
		} 
		fprintf(stderr, "\n"); 
		abort(); 
	}

	retarray[0] = (unsigned int)func_start; 
	retarray[1] = (unsigned int)cookie_address; 
	return( retarray ); 
}

