#ifndef VERSION_H
#define VERSION_H
  #ifndef HAS_GENERATED_VERSION
    #define GIT_BRANCH "Unknown"
    #define GIT_COMMIT_HASH "Unknown"
    #define GIT_TAGGED_VERSION "Unknown"
    #define GIT_TAG "Unknown"
    #define BUILD_DATE "Unknown"
    #define COPYRIGHT_DATE "2020"
    #define AUTHOR "Brad Hochgesang"
  #else
    #include "version.generated.h"
  #endif
#endif