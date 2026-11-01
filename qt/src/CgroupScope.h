#pragma once

#include <ghostty_ext.h>

// Lets libghostty move the child process of each terminal into its own
// transient systemd scope, as the GTK app does. The memory killer can then
// end one terminal instead of the whole application.
void installCgroupScopeCallback(ghostty_app_t app);
