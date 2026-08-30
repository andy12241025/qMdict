/* Hand-written replacement for the autotools-generated config.h.
 *
 * qMdict vendors only the Speex decoder, so this enables the floating-point
 * build and leaves out everything the encoder and the DSP library need. */
#ifndef QMDICT_SPEEX_CONFIG_H
#define QMDICT_SPEEX_CONFIG_H

#define FLOATING_POINT 1
#define USE_SMALLFT 1
#define EXPORT
#define HAVE_STDINT_H 1

/* Silence libspeex's diagnostics. A damaged clip inside a dictionary is
 * something the user is told about in the status bar; printing "notification:
 * Invalid mode specified in Speex header" to stderr helps nobody. The decoder
 * still reports the failure through its return values. */
#define OVERRIDE_SPEEX_FATAL
#define OVERRIDE_SPEEX_WARNING
#define OVERRIDE_SPEEX_WARNING_INT
#define OVERRIDE_SPEEX_NOTIFY

static inline void _speex_fatal(const char *str, const char *file, int line)
{
    (void)str;
    (void)file;
    (void)line;
}

static inline void speex_warning(const char *str) { (void)str; }

static inline void speex_warning_int(const char *str, int val)
{
    (void)str;
    (void)val;
}

static inline void speex_notify(const char *str) { (void)str; }

#endif
