/*
 * Link the modified nwmain with the NWMouse C code
 */

        .text

        .align  4,0x90
        .globl  NWMouse_link1_asm
NWMouse_link1_asm:
        mov %edi, _NWMouse_REGISTER

        /* replicate code we whacked */
	push 	%edi
	mov 	_NWMouse_ORIGTABLE, %edi
	movl	%edi, 0xc(%eax)
	pop	%edi

        pusha
        call    NWMouse_link1_c
        popa

        jmp     *__nwmouse_retaddr1

