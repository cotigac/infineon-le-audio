/*******************************************************************************
 * psoc_edge_e82.ld - Linker Script for PSoC Edge E82
 *
 * Memory layout for PSoC Edge E82 (Cortex-M55)
 *
 * Memory Map:
 *   FLASH:  0x10000000 - 0x1007FFFF (512 KB)
 *   SRAM:   0x20000000 - 0x204FFFFF (5 MB)
 *
 * Section Layout:
 *   .text       - Code and read-only data (Flash)
 *   .data       - Initialized data (SRAM, copied from Flash)
 *   .bss        - Uninitialized data (SRAM, zeroed)
 *   .heap       - Dynamic memory allocation (SRAM)
 *   .stack      - Main stack (SRAM)
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

/* Entry point */
ENTRY(Reset_Handler)

/*******************************************************************************
 * Memory Regions
 ******************************************************************************/
MEMORY
{
    /* Flash memory - 512 KB */
    FLASH (rx)      : ORIGIN = 0x10000000, LENGTH = 512K

    /* SRAM - 5 MB total */
    /* Split into regions for different purposes */

    /* Main SRAM for code/data - 2.5 MB */
    SRAM (rwx)      : ORIGIN = 0x20000000, LENGTH = 2560K

    /* Audio buffers - 1 MB (DMA-accessible) */
    AUDIO_RAM (rw)  : ORIGIN = 0x20280000, LENGTH = 1M

    /* Bluetooth buffers - 1 MB */
    BT_RAM (rw)     : ORIGIN = 0x20380000, LENGTH = 1M

    /* Stack and heap - 512 KB */
    STACK_RAM (rw)  : ORIGIN = 0x20480000, LENGTH = 512K
}

/*******************************************************************************
 * Stack and Heap Sizes
 ******************************************************************************/

/* Main stack size (used before FreeRTOS starts and in ISRs) */
__stack_size__ = 16K;

/* Heap size (for malloc/free - FreeRTOS has its own heap) */
__heap_size__ = 64K;

/* FreeRTOS heap (defined in FreeRTOSConfig.h) */
__freertos_heap_size__ = 256K;

/*******************************************************************************
 * Section Definitions
 ******************************************************************************/
SECTIONS
{
    /*--------------------------------------------------------------------------
     * Vector Table (must be at flash start)
     *------------------------------------------------------------------------*/
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /*--------------------------------------------------------------------------
     * Code Section
     *------------------------------------------------------------------------*/
    .text :
    {
        . = ALIGN(4);
        __text_start__ = .;

        *(.text)
        *(.text*)

        /* ARM exception handling */
        *(.ARM.extab*)
        *(.ARM.exidx*)

        /* C++ constructors */
        KEEP(*(.init))
        KEEP(*(.fini))

        /* C++ static constructors/destructors */
        . = ALIGN(4);
        KEEP(*crtbegin.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*crtend.o(.ctors))

        KEEP(*crtbegin.o(.dtors))
        KEEP(*(EXCLUDE_FILE(*crtend.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*crtend.o(.dtors))

        . = ALIGN(4);
        __text_end__ = .;
    } > FLASH

    /*--------------------------------------------------------------------------
     * Read-Only Data
     *------------------------------------------------------------------------*/
    .rodata :
    {
        . = ALIGN(4);
        __rodata_start__ = .;

        *(.rodata)
        *(.rodata*)

        /* Bluetooth firmware (if embedded) */
        KEEP(*(.bt_firmware))

        . = ALIGN(4);
        __rodata_end__ = .;
    } > FLASH

    /*--------------------------------------------------------------------------
     * ARM Exception Tables
     *------------------------------------------------------------------------*/
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    /*--------------------------------------------------------------------------
     * Preinit/Init/Fini Arrays (C++ static initialization)
     *------------------------------------------------------------------------*/
    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /*--------------------------------------------------------------------------
     * Copy table for data initialization
     *------------------------------------------------------------------------*/
    .copy.table :
    {
        . = ALIGN(4);
        __copy_table_start__ = .;

        LONG(LOADADDR(.data))
        LONG(__data_start__)
        LONG((__data_end__ - __data_start__) / 4)

        __copy_table_end__ = .;
    } > FLASH

    /*--------------------------------------------------------------------------
     * Zero table for BSS initialization
     *------------------------------------------------------------------------*/
    .zero.table :
    {
        . = ALIGN(4);
        __zero_table_start__ = .;

        LONG(__bss_start__)
        LONG((__bss_end__ - __bss_start__) / 4)

        __zero_table_end__ = .;
    } > FLASH

    /* End of flash used marker */
    __etext = .;

    /*--------------------------------------------------------------------------
     * Initialized Data (copied from Flash to SRAM at startup)
     *------------------------------------------------------------------------*/
    .data : AT(__etext)
    {
        . = ALIGN(4);
        __data_start__ = .;

        *(.data)
        *(.data*)

        /* FreeRTOS privileged data (if using MPU) */
        *(.privileged_data)

        . = ALIGN(4);
        __data_end__ = .;
    } > SRAM

    /*--------------------------------------------------------------------------
     * Uninitialized Data (zeroed at startup)
     *------------------------------------------------------------------------*/
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start__ = .;

        *(.bss)
        *(.bss*)
        *(COMMON)

        . = ALIGN(4);
        __bss_end__ = .;
    } > SRAM

    /*--------------------------------------------------------------------------
     * FreeRTOS Heap
     *------------------------------------------------------------------------*/
    .freertos_heap (NOLOAD) :
    {
        . = ALIGN(8);
        __freertos_heap_start__ = .;
        . = . + __freertos_heap_size__;
        __freertos_heap_end__ = .;
    } > SRAM

    /*--------------------------------------------------------------------------
     * Audio DMA Buffers (in dedicated audio RAM region)
     *------------------------------------------------------------------------*/
    .audio_buffers (NOLOAD) :
    {
        . = ALIGN(32);  /* DMA alignment */
        __audio_buffers_start__ = .;

        /* I2S DMA buffers */
        *(.i2s_buffers)
        *(.i2s_buffers*)

        /* LC3 codec buffers */
        *(.lc3_buffers)
        *(.lc3_buffers*)

        /* PCM audio buffers */
        *(.audio_buffers)
        *(.audio_buffers*)

        . = ALIGN(32);
        __audio_buffers_end__ = .;
    } > AUDIO_RAM

    /*--------------------------------------------------------------------------
     * Bluetooth Buffers
     *------------------------------------------------------------------------*/
    .bt_buffers (NOLOAD) :
    {
        . = ALIGN(4);
        __bt_buffers_start__ = .;

        /* HCI buffers */
        *(.hci_buffers)
        *(.hci_buffers*)

        /* ISOC buffers */
        *(.isoc_buffers)
        *(.isoc_buffers*)

        /* ACL buffers */
        *(.acl_buffers)
        *(.acl_buffers*)

        . = ALIGN(4);
        __bt_buffers_end__ = .;
    } > BT_RAM

    /*--------------------------------------------------------------------------
     * Heap (for malloc - separate from FreeRTOS heap)
     *------------------------------------------------------------------------*/
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        __heap_start__ = .;
        __end__ = .;
        end = .;
        . = . + __heap_size__;
        __heap_end__ = .;
        __HeapLimit = .;
    } > STACK_RAM

    /*--------------------------------------------------------------------------
     * Main Stack (MSP - used in handler mode and before RTOS starts)
     *------------------------------------------------------------------------*/
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        __stack_start__ = .;
        . = . + __stack_size__;
        . = ALIGN(8);
        __stack_end__ = .;
        __StackTop = .;
        __StackLimit = __stack_start__;
    } > STACK_RAM

    /* Initial stack pointer value (top of stack) */
    PROVIDE(__stack = __StackTop);

    /*--------------------------------------------------------------------------
     * Memory Validation
     *------------------------------------------------------------------------*/

    /* Check that all sections fit in memory */
    ASSERT(__stack_end__ <= ORIGIN(STACK_RAM) + LENGTH(STACK_RAM),
           "Stack overflow - increase STACK_RAM or reduce stack size")

    ASSERT(__freertos_heap_end__ <= ORIGIN(SRAM) + LENGTH(SRAM),
           "FreeRTOS heap overflow - reduce heap size")

    ASSERT(__audio_buffers_end__ <= ORIGIN(AUDIO_RAM) + LENGTH(AUDIO_RAM),
           "Audio buffers overflow AUDIO_RAM region")

    ASSERT(__bt_buffers_end__ <= ORIGIN(BT_RAM) + LENGTH(BT_RAM),
           "Bluetooth buffers overflow BT_RAM region")
}

/*******************************************************************************
 * Symbols for startup code
 ******************************************************************************/

/* SRAM limits */
__RAM_START__ = ORIGIN(SRAM);
__RAM_SIZE__ = LENGTH(SRAM);
__RAM_END__ = ORIGIN(SRAM) + LENGTH(SRAM);

/* Flash limits */
__ROM_START__ = ORIGIN(FLASH);
__ROM_SIZE__ = LENGTH(FLASH);
__ROM_END__ = ORIGIN(FLASH) + LENGTH(FLASH);

/* Audio RAM limits */
__AUDIO_RAM_START__ = ORIGIN(AUDIO_RAM);
__AUDIO_RAM_SIZE__ = LENGTH(AUDIO_RAM);
__AUDIO_RAM_END__ = ORIGIN(AUDIO_RAM) + LENGTH(AUDIO_RAM);

/* BT RAM limits */
__BT_RAM_START__ = ORIGIN(BT_RAM);
__BT_RAM_SIZE__ = LENGTH(BT_RAM);
__BT_RAM_END__ = ORIGIN(BT_RAM) + LENGTH(BT_RAM);
