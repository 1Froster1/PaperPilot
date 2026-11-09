#pragma once

#include <QString>

// Reads desktop.local.json next to the executable. The file is intentionally
// ignored by Git so each developer can use a different backend address.
QString backendBaseUrl();
