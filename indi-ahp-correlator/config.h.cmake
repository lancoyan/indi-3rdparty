/* The symbol timezone is an int, not a function */
#define TIMEZONE_IS_INT 1

/* Define if you have termios.h */
#cmakedefine   HAVE_TERMIOS_H 1

/* Define if you have fitsio.h */
#cmakedefine   HAVE_CFITSIO_H 1

/* Define Driver version */
#define AHP_CORRELATOR_VERSION_MAJOR @AHP_CORRELATOR_VERSION_MAJOR@
#define AHP_CORRELATOR_VERSION_MINOR @AHP_CORRELATOR_VERSION_MINOR@
