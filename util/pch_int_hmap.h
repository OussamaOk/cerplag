#ifndef H_PCH_INT_HMAP
#define H_PCH_INT_HMAP

typedef char* pch;

#define KTYPE pch
#define VTYPE int

#define KTYPED(x) pch_##x
#define VTYPED(x) int_##x
#define ITYPED(x) pch_int_##x

#include "hmap.gen.h"

#undef KTYPE
#undef VTYPE
#undef KTYPED
#undef VTYPED
#undef ITYPED

#endif  // H_PCH_INT_HMAP
