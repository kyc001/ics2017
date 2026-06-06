ENTRY(_start)

SECTIONS {
  . = 0x8048000;

  .text : {
    *(.text.unlikely)
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
    *(COMMON)
  }

  end = .;
  _end = .;

  /DISCARD/ : {
    *(.note.gnu.property)
    *(.note.GNU-stack)
    *(.comment)
    *(.eh_frame*)
  }
}
