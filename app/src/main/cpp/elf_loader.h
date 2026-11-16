#pragma once
#include <cstdint>

void installThreadAltStack();
void installCrashAndSvcHandlers();
void* loadSwitchElf(const char* elfPath);
void jumpToSwitchEntry(void* entryPoint);
uint8_t* allocateThreadSwitchTls(void* threadHandle);
