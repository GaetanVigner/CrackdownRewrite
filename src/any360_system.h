#pragma once

namespace crackdown {

// The system call groups the any360_sys_groups setting names (stateless, files, objects, threads, memory, xam, input,
// audio) switched to any360::sys. Call once the title is loaded, before its first instruction (OnPostSetup).
void StartAny360SystemIfAsked();

}  // namespace crackdown
