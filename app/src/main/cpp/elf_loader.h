#pragma once
void installCrashAndSvcHandlers();
void* loadSwitchElf(const char* elfPath);
void jumpToSwitchEntry(void* entryPoint);
