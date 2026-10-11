| NatFeats (emulator native features) for debug output under Hatari/ARAnyM.
| long nf_probe(void)  - supervisor only: NF id of "NF_STDERR", 0 if absent
| long nf_call2(long id, long arg)

	.text
	.globl	nf_probe
nf_probe:
	move.l	0x10.w,%a1		| illegal instruction vector
	move.l	%sp,%a0
	move.l	#1f,0x10.w
	pea	nf_name
	moveq	#0,%d0
	.dc.w	0x7300			| nf_id
	addq.l	#4,%sp
	move.l	%a1,0x10.w
	rts
1:	move.l	%a0,%sp			| no NatFeats: illegal instruction
	moveq	#0,%d0
	move.l	%a1,0x10.w
	rts

	.globl	nf_call2
nf_call2:
	.dc.w	0x7301			| nf_call(id, arg)
	rts

	.data
nf_name:
	.asciz	"NF_STDERR"
	.even
