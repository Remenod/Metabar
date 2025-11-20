section .bootstrap

global bootstrap_enable_paging
global bootstrap_enable_global_pages
global bootstrap_load_page_directory

bootstrap_enable_paging:
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ret

bootstrap_enable_global_pages:
    mov eax, cr4
    or eax, 1 << 7
    mov cr4, eax

    ret

bootstrap_load_page_directory:
    mov eax, [esp+4]
    mov cr3, eax

    ret