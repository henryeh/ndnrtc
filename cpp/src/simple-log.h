//
//  simple-log.h
//  ndnrtc
//
//  Copyright 2013 Regents of the University of California
//  For licensing details see the LICENSE file.
//
//  Author:  Peter Gusev 
//  Created: 8/8/13
//

#ifndef __ndnrtc__simple__
#define __ndnrtc__simple__

#include <iostream>

#if !defined(NDN_LOGGING)
#undef NDN_TRACE
#undef NDN_INFO
#undef NDN_WARN
#undef NDN_ERROR
#undef NDN_DEBUG
#endif

// if defieed detailed logging - print whole signature of the function.
#if defined(NDN_DETAILED)
#define __NDN_FNAME__ __PRETTY_FUNCTION__
#else
#define __NDN_FNAME__ __func__
#endif

#if defined (NDN_TRACE) && defined(DEBUG)
#define TRACE(fmt, ...) NdnLogger::log(__NDN_FNAME__, NdnLoggerLevelTrace, fmt, ##__VA_ARGS__)
#else
#define TRACE(fmt, ...)
#endif

#if defined(NDN_DEBUG) && defined(DEBUG)
#define DBG(fmt, ...) NdnLogger::log(__NDN_FNAME__, NdnLoggerLevelDebug, fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#if defined (NDN_INFO)
#define INFO(fmt, ...) NdnLogger::log(__NDN_FNAME__, NdnLoggerLevelInfo, fmt, ##__VA_ARGS__)
#else
#define INFO(fmt, ...)
#endif

#ifdef NDN_WARN
#define WARN(fmt, ...) NdnLogger::log(__NDN_FNAME__, NdnLoggerLevelWarning, fmt, ##__VA_ARGS__)
#else
#define WARN(fmt, ...)
#endif

#ifdef NDN_ERROR
#define ERR(fmt, ...) NdnLogger::log(__NDN_FNAME__, NdnLoggerLevelError, fmt, ##__VA_ARGS__)
#else
#define ERR(fmt, ...)
#endif

namespace ndnlog {
    typedef enum _NdnLoggerLevel {
        NdnLoggerLevelTrace = 0,
        NdnLoggerLevelDebug = 1,
        NdnLoggerLevelInfo = 2,
        NdnLoggerLevelWarning = 3,
        NdnLoggerLevelError = 4
    } NdnLoggerLevel;
    
    /**
     * Simple logger class to stdout (for now)
     */
    class NdnLogger
    {
    public:
        // construction/desctruction
        NdnLogger();
        ~NdnLogger();
        
        // public static attributes go here
        
        // public static methods go here
        static void log(const char *fName, NdnLoggerLevel level, const char *format, ...);
        
        // public attributes go here
        
        // public methods go here
    private:
        // private static attributes go here
        
        // private static methods go here
        static NdnLogger* getInstance();
        static const char* stingify(NdnLoggerLevel lvl);
        
        // private attributes go here
        char *buf;
        
        // private methods go here
        void log(const char *str);
        void flushBuffer(char *buf);
        char* getBuffer(){ return buf; }
    };
}

#endif /* defined(__ndnrtc__simple__) */