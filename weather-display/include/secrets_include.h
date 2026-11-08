/**
 * @file    secrets_include.h
 * @brief   Include the git-ignored secrets.h if present, else the example placeholders.
 */
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"  // Your Wi-Fi credentials and tokens (git-ignored).
#else
#include "secrets.example.h"  // Placeholders so a fresh clone builds; copy to secrets.h.
#endif
