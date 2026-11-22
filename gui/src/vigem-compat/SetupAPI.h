// ViGEmClient spells Windows headers as the Windows SDK does; mingw-w64 names them in lower
// case, which matters when cross-compiling on a case-sensitive file system
#include_next <setupapi.h>
