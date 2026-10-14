OUTPUT_ARCH( "riscv" )
ENTRY( _start)

SECTIONS
{
  . = 0x80000000;
  .text.init : { *(.text.init) }
  . = ALIGN(0x1000);
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .data : { *(.data) }
  .bss : { *(.bss) }
 _end = .;

 
  /DISCARD/ : {
    *(.reloc*)
    *(.idata*)
    *(.pdata*)
    *(.xdata*)
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }

}

