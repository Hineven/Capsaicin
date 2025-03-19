
#ifndef CAPSAICIN_EXPORT_H
#define CAPSAICIN_EXPORT_H

#ifdef CAPSAICIN_STATIC_DEFINE
#  define CAPSAICIN_EXPORT
#  define CAPSAICIN_NO_EXPORT
#else
#  ifndef CAPSAICIN_EXPORT
#    ifdef capsaicin_EXPORTS
        /* We are building this library */
#      define CAPSAICIN_EXPORT __declspec(dllexport)
#    else
        /* We are using this library */
#      define CAPSAICIN_EXPORT __declspec(dllimport)
#    endif
#  endif

#  ifndef CAPSAICIN_NO_EXPORT
#    define CAPSAICIN_NO_EXPORT 
#  endif
#endif

#ifndef CAPSAICIN_DEPRECATED
#  define CAPSAICIN_DEPRECATED __declspec(deprecated)
#endif

#ifndef CAPSAICIN_DEPRECATED_EXPORT
#  define CAPSAICIN_DEPRECATED_EXPORT CAPSAICIN_EXPORT CAPSAICIN_DEPRECATED
#endif

#ifndef CAPSAICIN_DEPRECATED_NO_EXPORT
#  define CAPSAICIN_DEPRECATED_NO_EXPORT CAPSAICIN_NO_EXPORT CAPSAICIN_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef CAPSAICIN_NO_DEPRECATED
#    define CAPSAICIN_NO_DEPRECATED
#  endif
#endif

#endif /* CAPSAICIN_EXPORT_H */
