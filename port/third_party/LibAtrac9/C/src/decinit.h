#pragma once

#include "error_codes.h"
#include "structures.h"

At9Status ResetDecoder(Atrac9Handle* handle); /* kr-port */
At9Status InitDecoder(Atrac9Handle* handle, unsigned char * configData, int wlength);
