// Gridline F1 — first-run / on-demand Wi-Fi setup (access point + web page)
#pragma once

// Runs the setup mode until credentials are saved, then restarts.
// Never returns.
void provisioningRun(bool canCancel);
