/* FreeType's bundled zlib has static cross-file helpers and is designed for
 * a single translation unit. Keep its sources unmodified and namespace all
 * external zlib symbols with Z_PREFIX (configured on this isolated target). */
#include <string.h>
#define ft_memcpy memcpy
#include "zutil.c"
#include "adler32.c"
#include "crc32.c"
#include "inftrees.c"
#include "inffast.c"
#include "inflate.c"
