#pragma once

/* A text editor the size of its name. Opens the file at `path`, or starts empty when there is no
 * such file yet, and writes it back on request. */
void atto_main(const char *path);
