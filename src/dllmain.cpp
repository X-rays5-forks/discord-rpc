#include <windows.h>

#ifdef WIN32
BOOL WINAPI DllMain(HMODULE, DWORD, LPVOID) {
  return TRUE;
}
#endif
