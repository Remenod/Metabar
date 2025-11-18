#pragma once

#include <lib/types.h>
#include <paging/page_directory.h>

void bootstrap_setup_mapping(void);
void bootstrap_setup_page_directory(void);
void bootstrap_enable_global_pages(void);
void bootstrap_enable_paging(void);