/*
 * strlen.s
 */
	.text

    .global strlen
    .type strlen, %function
    .align 4

/* Returns the length of the string (ISO). The previous version returned a pointer to the
   terminating zero instead, which every caller in this tree treats as a length, and GCC also
   turns counting loops into strlen calls. */
strlen:
    mov   r1, r0
1:  ldrb  r3, [r1], #1
    cmp   r3, #0
    bne   1b
    sub   r0, r1, r0
    sub   r0, r0, #1
    mov   pc, lr



