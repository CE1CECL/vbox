#include <ipxe/init.h>
#include <ipxe/serial.h>
#include <ipxe/console.h>
#include <config/console.h>

/** @file
 *
 * Serial console
 *
 */

/* Set default console usage if applicable */
#if ! ( defined ( CONSOLE_SERIAL ) && CONSOLE_EXPLICIT ( CONSOLE_SERIAL ) )
#undef CONSOLE_SERIAL
#define CONSOLE_SERIAL ( CONSOLE_USAGE_ALL & ~CONSOLE_USAGE_LOG )
#endif

struct console_driver serial_console __console_driver;

static void serial_console_init ( void ) {
	/* Serial driver initialization should already be done,
	 * time to enable the serial console. */
	serial_console.disabled = 0;
}

struct console_driver serial_console __console_driver = {
	.putchar = serial_putc,
	.getchar = serial_getc,
	.iskey = serial_ischar,
	.disabled = 1,
	.usage = CONSOLE_SERIAL,
};

/**
 * Serial console initialisation function
 */
struct init_fn serial_console_init_fn __init_fn ( INIT_CONSOLE ) = {
	.initialise = serial_console_init,
};
