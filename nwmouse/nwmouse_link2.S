/*
 * Link the modified nwmain with the NWMouse C code
 */

        .text

        .align  4,0x90
        .globl  NWMouse_link2_asm
NWMouse_link2_asm:
        mov %edx, _NWMouse_REGISTER

        /* replicate code we whacked */
	push 	%edx
	mov	_NWMouse_INCD, %edx
	incl	0x0(%edx)
	pop	%edx

        pusha
        call    NWMouse_link2_c
        popa

        jmp     *__nwmouse_retaddr2

