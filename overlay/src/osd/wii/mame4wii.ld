/*--------------------------------------------------------------------------------
	MAME4Wii linker script

	MAME does not fit in the 24 MB of MEM1, so code and read-only data are
	linked into MEM2.  Writable data stays in MEM1, because libogc and the
	other system libraries keep DMA buffers in .data/.bss and several
	devices (EXI, SI, AI) can only reach MEM1.  The writable data sits in
	a reserved area at the top of MEM1; the heap gets the rest of MEM1
	(below it) and the rest of MEM2 (above the code).

	The resulting DOL is unpacked by the boot stub (bootstub/), because
	loaders only accept executables inside the MEM1 executable area.
--------------------------------------------------------------------------------*/

OUTPUT_ARCH(powerpc:common)
ENTRY(__app_start)

PROVIDE_HIDDEN( __isIPL = 0 );

PROVIDE_HIDDEN( __ppc_excpt_buf   = 0x800000d0 );
PROVIDE_HIDDEN( __ppc_excpt_table = 0x80003000 );

/* where the image goes */
MEM2_CODE = 0x90002000;
MEM1_DATA = 0x81500000;
MEM1_TOP  = 0x81800000;

PHDRS {
	text   PT_LOAD FLAGS(5);
	rodata PT_LOAD FLAGS(4);
	data   PT_LOAD FLAGS(6);
	bss    PT_LOAD;
}

SECTIONS {

	/* ===================== MEM2: code and constants ===================== */

	. = MEM2_CODE;

	.init : {
		*(.crt0 .crt0.*)
	} :text = 0

	.text : ALIGN_WITH_INPUT {
		KEEP( *(SORT_NONE(.init)) )
		*(.text.unlikely .text.*_unlikely .text.unlikely.*)
		*(.text.exit .text.exit.*)
		*(.text.startup .text.startup.*)
		*(.text.hot .text.hot.*)
		*(SORT(.text.sorted.*))
		*(.text .iplt .stub .text.* .gnu.linkonce.t.*)
		KEEP( *(SORT_NONE(.fini)) )
		. = ALIGN(32);
	} :text = 0

	.rodata : {
		KEEP( *(.rodata.__sys_versioninfo) )
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.rodata1)
		*(.fixup)
		*(.got1)
		*(.got2)
		*(.branch_lt)
		*(.got)
		*(.plt)
	} :rodata = 0

	.sdata2 : ALIGN_WITH_INPUT {
		*(.sdata2 .sdata2.* .gnu.linkonce.s2.*)
		*(.sbss2 .sbss2.* .gnu.linkonce.sb2.*)
	} :rodata = 0

	/* Thread-local variables.  libogc gives every thread the same r2
	   (_SDA2_BASE_ = .sdata2 + 0x8000) and no TLS block of its own, so
	   local-exec TLS accesses (r2 - 0x7000 + offset) all end up at
	   .sdata2 + 0x1000.  Reserve that range as a zeroed, writable area
	   shared by all threads; otherwise those writes land in whatever
	   section follows (the unwinder's tables). */
	.tls_area ADDR(.sdata2) + 0x1000 : {
		__tls_area_start = .;
		BYTE(0)
		. = __tls_area_start + 0x400;
	} :rodata = 0

	.eh_frame_hdr : ALIGN_WITH_INPUT {
		*(.eh_frame_hdr)
		*(.eh_frame_entry .eh_frame_entry.*)
	} :rodata = 0

	.eh_frame : ALIGN_WITH_INPUT {
		KEEP( *(.eh_frame) )
		*(.eh_frame.*)
	} :rodata = 0

	.gcc_except_table : ALIGN_WITH_INPUT {
		*(.gcc_except_table .gcc_except_table.*)
	} :rodata = 0

	.gnu_extab : ALIGN_WITH_INPUT {
		*(.gnu_extab*)
	} :rodata = 0

	.ctors : ALIGN_WITH_INPUT {
		KEEP( *ecrti.o(.ctors) )
		KEEP( *crtbegin.o(.ctors) )
		KEEP( *crtbegin?.o(.ctors) )
		KEEP( *(EXCLUDE_FILE(*ecrtn.o *crtend.o *crtend?.o) .ctors) )
		KEEP( *(SORT_BY_NAME(.ctors.*)) )
		KEEP( *(SORT_NONE(.ctors)) )
	} :rodata = 0

	.dtors : ALIGN_WITH_INPUT {
		KEEP( *ecrti.o(.dtors) )
		KEEP( *crtbegin.o(.dtors) )
		KEEP( *crtbegin?.o(.dtors) )
		KEEP( *(EXCLUDE_FILE(*ecrtn.o *crtend.o *crtend?.o) .dtors) )
		KEEP( *(SORT_BY_NAME(.dtors.*)) )
		KEEP( *(SORT_NONE(.dtors)) )
		. = ALIGN(32);
	} :rodata = 0

	PROVIDE_HIDDEN( __mem2_image_end = . );
	PROVIDE_HIDDEN( __Arena2Lo    = (. + 0x1f) &~ 0x1f );
	PROVIDE_HIDDEN( __Arena2Hi    = 0x933e0000 );
	PROVIDE_HIDDEN( __ipcbufferLo = 0x933e0000 );
	PROVIDE_HIDDEN( __ipcbufferHi = 0x93400000 );

	/* ===================== MEM1: writable data ===================== */

	. = MEM1_DATA;

	.data : ALIGN_WITH_INPUT {
		*(.data .data.* .gnu.linkonce.d.*)
		*(.data1)
		SORT(CONSTRUCTORS)
	} :data = 0

	.sdata : ALIGN_WITH_INPUT {
		*(.sdata .sdata.* .gnu.linkonce.s.*)
		*(.lit8)
		*(.lit4)
		. = ALIGN(32);
	} :data = 0

	/* thread-local templates: only their layout is used (see .tls_area) */
	.tdata : { *(.tdata .tdata.* .gnu.linkonce.td.*) } :data
	.tbss  : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) } :data

	.sbss . (NOLOAD) : {
		*(.dynsbss)
		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
	} :bss

	PROVIDE_HIDDEN( __sbss_start = ADDR(.sbss)                 );
	PROVIDE_HIDDEN( __sbss_end   = ADDR(.sbss) + SIZEOF(.sbss) );

	.bss . (NOLOAD) : {
		*(.dynbss)
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(32);
	} :bss

	PROVIDE_HIDDEN( __bss_start = ADDR(.bss)                );
	PROVIDE_HIDDEN( __bss_end   = ADDR(.bss) + SIZEOF(.bss) );

	/* the heap gets MEM1 below the data area (above the exception table) */
	PROVIDE_HIDDEN( __Arena1Lo = 0x80004000 );
	PROVIDE_HIDDEN( __Arena1Hi = MEM1_DATA  );

	/* keep 64 KB at the top of MEM1 for the loader's command line */
	ASSERT( __bss_end <= MEM1_TOP - 0x10000, "MEM1 data area too large: move MEM1_DATA down" )
	ASSERT( __mem2_image_end <= 0x93000000, "image too large for MEM2" )
	ASSERT( SIZEOF(.sdata2) <= 0x1000, ".sdata2 overlaps the thread-local area" )
	ASSERT( SIZEOF(.tdata) == 0, "initialised thread-local data is not supported" )
	ASSERT( SIZEOF(.tbss) <= 0x400, "thread-local data larger than the reserved area" )

	/* ====================== debugging metadata ====================== */

	.stab           0 : { *(.stab) }
	.stabstr        0 : { *(.stabstr) }
	.stab.excl      0 : { *(.stab.excl) }
	.stab.exclstr   0 : { *(.stab.exclstr) }
	.stab.index     0 : { *(.stab.index) }
	.stab.indexstr  0 : { *(.stab.indexstr) }
	.comment 0 (INFO) : { *(.comment); LINKER_VERSION; }
	.gnu.build.attributes : { *(.gnu.build.attributes .gnu.build.attributes.*) }
	.debug          0 : { *(.debug) }
	.line           0 : { *(.line) }
	.debug_srcinfo  0 : { *(.debug_srcinfo) }
	.debug_sfnames  0 : { *(.debug_sfnames) }
	.debug_aranges  0 : { *(.debug_aranges) }
	.debug_pubnames 0 : { *(.debug_pubnames) }
	.debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }
	.debug_macinfo  0 : { *(.debug_macinfo) }
	.debug_weaknames 0 : { *(.debug_weaknames) }
	.debug_funcnames 0 : { *(.debug_funcnames) }
	.debug_typenames 0 : { *(.debug_typenames) }
	.debug_varnames  0 : { *(.debug_varnames) }
	.debug_pubtypes 0 : { *(.debug_pubtypes) }
	.debug_ranges   0 : { *(.debug_ranges) }
	.debug_macro    0 : { *(.debug_macro) }
	.debug_addr     0 : { *(.debug_addr) }
	.debug_line_str 0 : { *(.debug_line_str) }
	.debug_loclists 0 : { *(.debug_loclists) }
	.debug_rnglists 0 : { *(.debug_rnglists) }
	.debug_str_offsets 0 : { *(.debug_str_offsets) }
	.debug_sup      0 : { *(.debug_sup) }

	/DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.gnu.lto_*) }
}
