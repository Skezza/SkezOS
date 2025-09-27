/*
 * Multiboot2 header for the tiny kernel.
 *
 * GRUB will look for this structure at the beginning of the image and
 * load the kernel accordingly.  We keep the header very small: just
 * a magic number, architecture specifier, length and checksum.  The
 * terminating tag signals the end of the header.
 */
	.section .multiboot2_header
	.align 8
	/*
	 * See the Multiboot2 specification for details on these values.
	 * MAGIC identifies the file as Multiboot2 compliant, ARCH=0 for i386
	 * and HEADER_LEN gives the total header size.  The checksum makes the
	 * sum of the three fields equal zero.
	 */
	.set MAGIC,        0xe85250d6
	.set ARCH,         0
	.set HEADER_LEN,   24
	.set CHECKSUM,    -(MAGIC + ARCH + HEADER_LEN)

	.long MAGIC
	.long ARCH
	.long HEADER_LEN
	.long CHECKSUM

	/* End tag – required by the specification */
	.word 0
	.word 0
	.long 8
