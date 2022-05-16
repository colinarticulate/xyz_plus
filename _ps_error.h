
#ifndef _PS_ERROR_H
#define _PS_ERROR_H


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include <xyzsphinxbase/err.h>




class PSErrorHandler {
    //private:
        

    public:
        char _msg[2048];


        void
        err_msg_system(err_lvl_t lvl, const char *path, long ln, const char *fmt, ...)
        {
            // C++ no-logging: 
            int local_errno = errno;
            
            static const char *err_prefix[ERR_MAX] = {
                "DEBUG", "INFO", "INFOCONT", "WARN", "ERROR", "FATAL"
            };

            char msg[1024];
            va_list ap;

            // if (!err_cb)
            //     return;

            va_start(ap, fmt);
            vsnprintf(msg, sizeof(msg), fmt, ap);
            va_end(ap);

            if (path) {
                const char *fname = path2basename(path);
                if (lvl == ERR_INFOCONT)
            	    err_cb(lvl, "%s(%ld): %s: %s\n", fname, ln, msg, strerror(local_errno));
                else if (lvl == ERR_INFO)
                    err_cb(lvl, "%s: %s(%ld): %s: %s\n", err_prefix[lvl], fname, ln, msg, strerror(local_errno));
                else
            	    err_cb(lvl, "%s: \"%s\", line %ld: %s: %s\n", err_prefix[lvl], fname, ln, msg, strerror(local_errno));
            } else {
                err_cb(lvl, "%s: %s\n", msg, strerror(local_errno));
            }
        }

        void
        err_msg(err_lvl_t lvl, const char *path, long ln, const char *fmt, ...)
        {
            // C++ no-logging:
            static const char *err_prefix[ERR_MAX] = {
                "DEBUG", "INFO", "INFOCONT", "WARN", "ERROR", "FATAL"
            };

            char msg[1024];
            va_list ap;

            // if (!err_cb)
            //     return;

            va_start(ap, fmt);
            vsnprintf(msg, sizeof(msg), fmt, ap);
            va_end(ap);

            if (path) {
                const char *fname = path2basename(path);
                if (lvl == ERR_INFOCONT)
            	    err_cb(lvl, "%s(%ld): %s", fname, ln, msg);
                else if (lvl == ERR_INFO)
                    err_cb(lvl, "%s: %s(%ld): %s", err_prefix[lvl], fname, ln, msg);
                else
            	    err_cb(lvl, "%s: \"%s\", line %ld: %s", err_prefix[lvl], fname, ln, msg);
            } else {
                err_cb(lvl, "%s", msg);
            }
        }

        void
        //err_logfp_cb(void *user_data, err_lvl_t lvl, const char *fmt, ...)
        err_cb(err_lvl_t lvl, const char *fmt, ...)
        {
            va_list ap;
                                 
            va_start(ap, fmt);
            vsnprintf(_msg, sizeof(_msg), fmt, ap);
            va_end(ap);
           
        }

};

#endif

