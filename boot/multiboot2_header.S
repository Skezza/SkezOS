/*
 * Multiboot2 header for the tiny kernel.
 *
 * GRUB will look for this structure at the beginning of the image and
 * load the kernel accordingly.  We keep the header small: the fixed
 * Multiboot2 preamble, an optional framebuffer request, and the
 * terminating tag.
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
	.set HEADER_LEN,   48
	.set CHECKSUM,    -(MAGIC + ARCH + HEADER_LEN)

	.long MAGIC
	.long ARCH
	.long HEADER_LEN
	.long CHECKSUM

	/* Optional framebuffer request tag.  Keep it non-required so the
	 * kernel still boots cleanly with VGA fallback if a pixel surface
	 * cannot be provided.
	 */
	.word 5          /* tag type: framebuffer */
	.word 1          /* flags: optional */
	.long 20         /* tag size */
	.long 1024       /* preferred width */
	.long 768        /* preferred height */
	.long 32         /* preferred depth */

	.align 8
	/* End tag – required by the specification */
	.word 0
	.word 0
	.long 8
