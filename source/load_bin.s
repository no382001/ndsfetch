@ embed the ARM7 bootloader binary (load.bin) as a read-only data blob.

	.section .rodata, "a"
	.align 4
	.global load_bin
	.global load_bin_size

load_bin:
	.incbin "load.bin"
load_bin_end:

	.align 4
load_bin_size:
	.word load_bin_end - load_bin
