/* ----------------------------------------------------------------
 *  
 * Andor / ZwoGuider --> ZwoGcam / gcamzwo
 *
 * ---------------------------------------------------------------- */

#define PROJECT_ID      22             /* andorgui / zwogcam */
#define P_VERSION       "0.412"

extern void message(const void*,const char*,int);

#define SERVER_PORT     52311          /* zwoserver */

#define MSS_FILE        0x0001
#define MSS_FLUSH       0x0003

#define MSS_WINDOW      0x0100
#define MSS_YELLOW      0x0200
#define MSS_RED         0x0400
#define MSS_OVERWRITE   0x0800

#define MSS_INFO        (MSS_FLUSH | MSS_WINDOW)
#define MSS_WARN        (MSS_WINDOW | MSS_YELLOW)
#define MSS_ERROR       (MSS_FLUSH | MSS_WINDOW | MSS_RED)
#define MSS_OVER        (MSS_WINDOW | MSS_OVERWRITE)

enum guider_error_codes { E_ERROR=200,
                          E_NOTINIT,
                          E_RUNNING,
                          E_MISSPAR,
                          E_NOTACQ,
                          E_INCFRAME,
                          E_TELIO,
                          E_NOTIMP };

#define MAX_EXPTIME     30

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

