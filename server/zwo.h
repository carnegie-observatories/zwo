/* ----------------------------------------------------------------
 *  
 * ZWO/ASI camera
 *
 * ---------------------------------------------------------------- */

#define PROJECT_ID      23
#define P_VERSION       "0.032"

extern void message(const void*,const char*,int);

#define SERVER_PORT     (50011+(100*PROJECT_ID))

#define MSG_FILE        0x0001
#define MSG_FLUSH       0x0003
#define MSG_CLOSE       0x0004

#define MSG_WINDOW      0x0100
#define MSG_YELLOW      0x0200
#define MSG_RED         0x0400
#define MSG_OVERWRITE   0x0800

#define MSG_INFO        (MSG_FLUSH | MSG_WINDOW) //xxx
#define MSG_WARN        (MSG_WINDOW | MSG_YELLOW)
#define MSG_ERROR       (MSG_FLUSH | MSG_WINDOW | MSG_RED)
#define MSG_OVER        (MSG_WINDOW | MSG_OVERWRITE)

enum zwo_error_codes {  E_ERROR=200,
                        E_NOTINIT,
                        E_RUNNING,
                        E_MISSPAR,
                        E_NOTACQ,
                        E_INCFRAME,
                        E_FILTER,
                        E_NOTIMP };

#define MAX_EXPTIME     30
#define MIN_EXPTIME     0.0001

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

